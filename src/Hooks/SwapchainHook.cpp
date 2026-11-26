#include "SwapchainHook.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <kiero.h>
#include <minhook/include/MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "../Core/CaptureManager.hpp"
#include "../SDK/BedrockGame.hpp"
#include "../UI/SchematicMenu.hpp"
#include "../UI/UiAssets.hpp"
#include "../Utils/ConfigManager.hpp"
#include "../Utils/CrashGuard.hpp"
#include "../Utils/Logger.hpp"
#include "../Utils/Memory.hpp"
#include "../Utils/SignatureAndOffsetManager.hpp"
#include "../Utils/SoundManager.hpp"
#include "../Utils/Theme.hpp"
#include "../Utils/ToastManager.hpp"
#include "../Utils/Utils.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace SwapchainHook {

static int g_MenuToggleKeyVal = 'M';
static std::atomic<bool> g_bCapturingKeybind{false};
static thread_local bool t_InOurOverlay = false;
static POINT g_FrozenCursorPos{0, 0};

struct OverlayCallScope {
  bool prev;
  OverlayCallScope() : prev(t_InOurOverlay) { t_InOurOverlay = true; }
  ~OverlayCallScope() { t_InOurOverlay = prev; }
};

int g_MenuToggleKey = 'M';

static std::atomic<bool> g_bInitialized{false};
static std::atomic<bool> g_bImGuiInit{false};
static std::atomic<bool> g_bMenuOpen{false};
static std::atomic<bool> g_bCursorHooksInstalled{false};

static GraphicsAPI g_CurrentAPI = GraphicsAPI::Unknown;
static HWND g_hGameWindow = nullptr;
static WNDPROC g_oWndProc = nullptr;

static ImFont *g_pFontRegular = nullptr;
static ImFont *g_pFontBold = nullptr;
static ImFont *g_pFontMinecraft = nullptr;

typedef HRESULT(__stdcall *PresentFn)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(__stdcall *ResizeBuffersFn)(IDXGISwapChain *, UINT, UINT, UINT,
                                            DXGI_FORMAT, UINT);
typedef HRESULT(__stdcall *ResizeBuffers1Fn)(IDXGISwapChain3 *, UINT, UINT,
                                             UINT, DXGI_FORMAT, UINT,
                                             const UINT *, IUnknown *const *);
typedef void(__stdcall *ExecuteCommandListsFn)(ID3D12CommandQueue *, UINT,
                                               ID3D12CommandList *const *);

static PresentFn g_oPresent = nullptr;
static ResizeBuffersFn g_oResizeBuffers = nullptr;
static ResizeBuffers1Fn g_oResizeBuffers1 = nullptr;
static ExecuteCommandListsFn g_oExecuteCommandLists = nullptr;

static ID3D11Device *g_pd3d11Device = nullptr;
static ID3D11DeviceContext *g_pd3d11Context = nullptr;
static ID3D11RenderTargetView *g_pd3d11RTV = nullptr;

static ID3D12Device *g_pd3d12Device = nullptr;
static ID3D12CommandQueue *g_pd3d12CommandQueue = nullptr;
static ID3D12DescriptorHeap *g_pd3d12RtvDescHeap = nullptr;
static ID3D12DescriptorHeap *g_pd3d12SrvDescHeap = nullptr;
static ID3D12GraphicsCommandList *g_pd3d12CmdList = nullptr;
static ID3D12Fence *g_pd3d12Fence = nullptr;
static HANDLE g_d3d12FenceEvent = nullptr;
static UINT64 g_d3d12FenceCounter = 0;
static std::vector<ID3D12CommandAllocator *> g_d3d12Allocators;
static std::vector<ID3D12Resource *> g_d3d12RenderTargets;
static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> g_d3d12RtvHandles;
static std::vector<UINT64> g_d3d12FrameFenceValues;
static UINT g_d3d12BufferCount = 0;

static constexpr UINT MAX_SRV_DESCRIPTORS = 128;
static UINT g_d3d12SrvIncrement = 0;
static UINT g_d3d12SrvAllocIndex = 0;

static void AllocD3D12Srv(D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                          D3D12_GPU_DESCRIPTOR_HANDLE *outGpu) {
  D3D12_CPU_DESCRIPTOR_HANDLE cpu =
      g_pd3d12SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu =
      g_pd3d12SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(g_d3d12SrvAllocIndex) * g_d3d12SrvIncrement;
  gpu.ptr += static_cast<UINT64>(g_d3d12SrvAllocIndex) * g_d3d12SrvIncrement;
  g_d3d12SrvAllocIndex = (g_d3d12SrvAllocIndex + 1) % MAX_SRV_DESCRIPTORS;
  *outCpu = cpu;
  *outGpu = gpu;
}

static void SrvDescriptorAllocImGui(ImGui_ImplDX12_InitInfo *,
                                    D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                                    D3D12_GPU_DESCRIPTOR_HANDLE *outGpu) {
  AllocD3D12Srv(outCpu, outGpu);
}

static void SrvDescriptorFreeImGui(ImGui_ImplDX12_InitInfo *,
                                   D3D12_CPU_DESCRIPTOR_HANDLE,
                                   D3D12_GPU_DESCRIPTOR_HANDLE) {}

static std::atomic<ULONGLONG> g_LastRenderLevelMs{0};
static std::atomic<ULONGLONG> g_LastTurnDeltaMs{0};
static std::atomic<ULONGLONG> g_LastHudScreenMs{0};
static std::atomic<ULONGLONG> g_LastBlockingScreenMs{0};
static std::atomic<ULONGLONG> g_LastClipLockMs{0};
static std::atomic<ULONGLONG> g_LastSetCursorPosMs{0};
static std::atomic<ULONGLONG> g_LastShowCursorFalseMs{0};
static std::atomic<bool> g_bGameClipLocked{false};
static std::atomic<bool> g_bGameCursorHidden{false};
static std::atomic<bool> g_bRenderLevelHooked{false};
static std::atomic<bool> g_bScreenHooked{false};
static std::atomic<bool> g_bHasScreenName{false};
static std::atomic<bool> g_bInHudScreen{false};
static std::atomic<bool> g_bNeedReleaseMousePackets{false};
static std::mutex g_ScreenNameMtx;
static std::string g_CurrentScreenName = "unknown";

static BOOL(WINAPI *g_oClipCursor)(const RECT *) = nullptr;
static BOOL WINAPI hkClipCursor(const RECT *lpRect) {
  if (g_bMenuOpen.load())
    return FALSE;
  if (lpRect != nullptr) {
    bool smallClip = (lpRect->right - lpRect->left) <= 64 &&
                     (lpRect->bottom - lpRect->top) <= 64;
    g_bGameClipLocked.store(true);
    if (smallClip || lpRect->right > lpRect->left) {
      g_LastClipLockMs.store(GetTickCount64());
    }
  } else {
    g_bGameClipLocked.store(false);
  }
  return g_oClipCursor ? g_oClipCursor(lpRect) : TRUE;
}

static BOOL(WINAPI *g_oSetCursorPos)(int, int) = nullptr;
static BOOL WINAPI hkSetCursorPos(int x, int y) {
  if (g_bMenuOpen.load())
    return FALSE;
  g_LastSetCursorPosMs.store(GetTickCount64());
  return g_oSetCursorPos ? g_oSetCursorPos(x, y) : SetCursorPos(x, y);
}

static HWND(WINAPI *g_oSetCapture)(HWND) = nullptr;
static HWND WINAPI hkSetCapture(HWND hWnd) {
  if (g_bMenuOpen.load())
    return nullptr;
  return g_oSetCapture ? g_oSetCapture(hWnd) : SetCapture(hWnd);
}

static int(WINAPI *g_oShowCursor)(BOOL) = nullptr;
static int WINAPI hkShowCursor(BOOL bShow) {
  if (g_bMenuOpen.load()) {
    if (t_InOurOverlay) {
      return g_oShowCursor ? g_oShowCursor(bShow) : ShowCursor(bShow);
    }
    if (g_oShowCursor) {
      int res = 0;
      for (int i = 0; i < 8; ++i) {
        res = g_oShowCursor(TRUE);
        if (res >= 0)
          break;
      }
      return res;
    }
    return 0;
  }
  int res = g_oShowCursor ? g_oShowCursor(bShow) : ShowCursor(bShow);
  if (!bShow || res < 0) {
    g_bGameCursorHidden.store(true);
    g_LastShowCursorFalseMs.store(GetTickCount64());
  } else {
    g_bGameCursorHidden.store(false);
  }
  return res;
}

static bool IsBlockedPollKey(int vKey) {
  switch (vKey) {
  case VK_LBUTTON:
  case VK_RBUTTON:
  case VK_MBUTTON:
  case VK_XBUTTON1:
  case VK_XBUTTON2:
  case 'W':
  case 'A':
  case 'S':
  case 'D':
  case 'E':
  case 'Q':
  case 'F':
  case 'T':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case VK_SPACE:
  case VK_SHIFT:
  case VK_LSHIFT:
  case VK_RSHIFT:
  case VK_CONTROL:
  case VK_LCONTROL:
  case VK_RCONTROL:
    return true;
  default:
    return false;
  }
}

static BOOL(WINAPI *g_oGetCursorPos)(LPPOINT) = nullptr;
static BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint) {
  if (g_bMenuOpen.load() && !t_InOurOverlay && lpPoint) {
    *lpPoint = g_FrozenCursorPos;
    return TRUE;
  }
  return g_oGetCursorPos ? g_oGetCursorPos(lpPoint) : GetCursorPos(lpPoint);
}

static SHORT(WINAPI *g_oGetAsyncKeyState)(int) = nullptr;
static SHORT WINAPI hkGetAsyncKeyState(int vKey) {
  if (g_bMenuOpen.load() && !t_InOurOverlay && IsBlockedPollKey(vKey)) {
    return 0;
  }
  return g_oGetAsyncKeyState ? g_oGetAsyncKeyState(vKey)
                             : GetAsyncKeyState(vKey);
}

static SHORT(WINAPI *g_oGetKeyState)(int) = nullptr;
static SHORT WINAPI hkGetKeyState(int nVirtKey) {
  if (g_bMenuOpen.load() && !t_InOurOverlay && IsBlockedPollKey(nVirtKey)) {
    return 0;
  }
  return g_oGetKeyState ? g_oGetKeyState(nVirtKey) : GetKeyState(nVirtKey);
}

static UINT(WINAPI *g_oGetRawInputData)(HRAWINPUT, UINT, LPVOID, PUINT,
                                        UINT) = nullptr;
static UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand,
                                     LPVOID pData, PUINT pcbSize,
                                     UINT cbSizeHeader) {
  UINT ret =
      g_oGetRawInputData
          ? g_oGetRawInputData(hRawInput, uiCommand, pData, pcbSize,
                               cbSizeHeader)
          : GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
  if (g_bMenuOpen.load() && !t_InOurOverlay && ret != static_cast<UINT>(-1) &&
      pData != nullptr && uiCommand == RID_INPUT) {
    RAWINPUT *raw = reinterpret_cast<RAWINPUT *>(pData);
    if (raw->header.dwType == RIM_TYPEMOUSE) {
      raw->data.mouse.lLastX = 0;
      raw->data.mouse.lLastY = 0;
      raw->data.mouse.usButtonFlags = 0;
      raw->data.mouse.usButtonData = 0;
      raw->data.mouse.ulRawButtons = 0;
    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
      USHORT vk = raw->data.keyboard.VKey;
      if (vk != VK_ESCAPE && vk != static_cast<USHORT>(g_MenuToggleKey)) {
        raw->data.keyboard.VKey = 0;
        raw->data.keyboard.Flags |= RI_KEY_BREAK;
      }
    }
  }
  return ret;
}

static UINT(WINAPI *g_oGetRawInputBuffer)(PRAWINPUT, PUINT, UINT) = nullptr;
static UINT WINAPI hkGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize,
                                       UINT cbSizeHeader) {
  if (g_bMenuOpen.load() && !t_InOurOverlay) {
    return 0;
  }
  return g_oGetRawInputBuffer
             ? g_oGetRawInputBuffer(pData, pcbSize, cbSizeHeader)
             : GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
}

static constexpr UINT WM_APP_SYNC_CURSOR = WM_APP + 0x43;

static std::atomic<void *> g_pXWindow{nullptr};
static std::atomic<void *> g_pMouseDevice{nullptr};
static std::atomic<bool> g_bNeedClearCursorAfterClose{false};
static std::atomic<bool> g_bLoggedPtrBlock{false};
static std::atomic<bool> g_bLoggedRawBlock{false};
static std::atomic<bool> g_bLoggedBtnBlock{false};
static std::atomic<bool> g_bLoggedMoveBlock{false};
static std::atomic<bool> g_bLoggedFeedBlock{false};

static BOOL(WINAPI *g_oPeekMessageW)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
static BOOL WINAPI hkPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                  UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL ret = g_oPeekMessageW ? g_oPeekMessageW(lpMsg, hWnd, wMsgFilterMin,
                                               wMsgFilterMax, wRemoveMsg)
                             : PeekMessageW(lpMsg, hWnd, wMsgFilterMin,
                                            wMsgFilterMax, wRemoveMsg);
  if (ret && lpMsg && g_bMenuOpen.load() && !t_InOurOverlay) {
    UINT m = lpMsg->message;
    bool isRawOrPointer =
        (m == WM_INPUT || m == 0x00FE || (m >= 0x0241 && m <= 0x0257));
    bool isMouseMsg = ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) ||
                       (m >= WM_NCMOUSEMOVE && m <= WM_NCMBUTTONDBLCLK));
    if (isRawOrPointer) {
      lpMsg->message = WM_NULL;
      lpMsg->wParam = 0;
      lpMsg->lParam = 0;
    } else if (isMouseMsg && lpMsg->hwnd != g_hGameWindow) {
      if ((wRemoveMsg & PM_REMOVE) &&
          (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) && g_hGameWindow &&
          GetCurrentThreadId() ==
              GetWindowThreadProcessId(g_hGameWindow, nullptr)) {
        LPARAM lp = lpMsg->lParam;
        if (m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL) {
          OverlayCallScope scope;
          POINT pt{};
          if (g_oGetCursorPos ? g_oGetCursorPos(&pt) : GetCursorPos(&pt)) {
            ScreenToClient(g_hGameWindow, &pt);
            lp = MAKELPARAM(pt.x, pt.y);
          }
        }
        OverlayCallScope scope;
        ImGui_ImplWin32_WndProcHandler(g_hGameWindow, m, lpMsg->wParam, lp);
      }
      lpMsg->message = WM_NULL;
      lpMsg->wParam = 0;
      lpMsg->lParam = 0;
    }
  }
  return ret;
}

static BOOL(WINAPI *g_oGetPointerType)(UINT32, void *) = nullptr;
static BOOL WINAPI hkGetPointerType(UINT32 pointerId, void *pointerType) {
  if (g_bMenuOpen.load() && !t_InOurOverlay) {
    return FALSE;
  }
  return g_oGetPointerType ? g_oGetPointerType(pointerId, pointerType) : FALSE;
}

struct MouseInputPacket {
  int16_t x;
  int16_t y;
  int pad;
  int8_t type;
  int8_t state;
  char pad2[0x8];
};

typedef void(__thiscall *RenderLevelFn)(void *, void *, void *);
typedef void(__thiscall *SetupAndRenderFn)(void *, void *);
typedef void(__thiscall *ApplyTurnDeltaFn)(void *, void *);
typedef void *(__thiscall *InputHandlerTickFn)(void *, void *, void *, void *);
typedef void(__thiscall *MouseFeedFn)(void *, char, char, short, short, short,
                                      short, char);
typedef void *(__thiscall *GameCoreMouseInputFn)(void *, void *, void *,
                                                 void *);
typedef void *(__fastcall *GameCoreEventHandlerFn)(void *, void *, void *);
typedef void(__fastcall *GameCoreCursorFn)(void *);

static RenderLevelFn g_oRenderLevel = nullptr;
static SetupAndRenderFn g_oSetupAndRender = nullptr;
static ApplyTurnDeltaFn g_oApplyTurnDelta = nullptr;
static InputHandlerTickFn g_oInputHandlerTick = nullptr;
static MouseFeedFn g_oMouseFeed = nullptr;
static GameCoreMouseInputFn g_oGameCoreMouse = nullptr;
static GameCoreEventHandlerFn g_oGameCorePointer = nullptr;
static GameCoreEventHandlerFn g_oGameCoreRawMouse = nullptr;
static GameCoreEventHandlerFn g_oGameCoreMouseBtn = nullptr;
static GameCoreEventHandlerFn g_oGameCoreMouseMove = nullptr;
static GameCoreCursorFn g_oGrabMouse = nullptr;
static GameCoreCursorFn g_oReleaseMouse = nullptr;
static std::vector<MouseInputPacket> *g_pMouseInputVector = nullptr;

static bool IsValidPtrRange(uintptr_t ptr) {
  return ptr > 0x10000ULL && ptr < 0x00007FFFFFFFFFFFULL;
}

static bool TryReadMsvcString(uintptr_t strAddr, char *outBuf, size_t cap) {
  if (!IsValidPtrRange(strAddr) || cap < 8)
    return false;
  __try {
    const size_t size = *reinterpret_cast<const size_t *>(strAddr + 0x10);
    const size_t capRes = *reinterpret_cast<const size_t *>(strAddr + 0x18);
    if (size == 0 || size >= cap || capRes < size || capRes > 2048)
      return false;
    const char *src = (capRes >= 16)
                          ? *reinterpret_cast<const char *const *>(strAddr)
                          : reinterpret_cast<const char *>(strAddr);
    if (capRes >= 16 && !IsValidPtrRange(reinterpret_cast<uintptr_t>(src)))
      return false;
    for (size_t i = 0; i < size; ++i) {
      char c = src[i];
      if (c < 0x20 || c > 0x7E)
        return false;
      outBuf[i] = c;
    }
    outBuf[size] = '\0';
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool TryReadScreenLayerName(const void *screenView, int visualTreeOff,
                                   int rootOff, int layerNameOff, char *outBuf,
                                   size_t cap) {
  if (!screenView)
    return false;
  uintptr_t sv = reinterpret_cast<uintptr_t>(screenView);
  if (!IsValidPtrRange(sv))
    return false;
  uintptr_t vt = 0;
  uintptr_t root = 0;
  __try {
    vt = *reinterpret_cast<const uintptr_t *>(sv + visualTreeOff);
    if (!IsValidPtrRange(vt))
      return false;
    root = *reinterpret_cast<const uintptr_t *>(vt + rootOff);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  if (!IsValidPtrRange(root))
    return false;
  const int candidates[4] = {layerNameOff, 0x20, 0x18, 0x28};
  for (int i = 0; i < 4; ++i) {
    if (TryReadMsvcString(root + candidates[i], outBuf, cap)) {
      if (strstr(outBuf, "screen") != nullptr) {
        return true;
      }
    }
  }
  return false;
}

typedef void(__fastcall *LevelRendererPlayerRenderFn)(void *, void *, void *,
                                                       void *);
static LevelRendererPlayerRenderFn g_oLevelRendererPlayerRender = nullptr;

static void __fastcall hkLevelRendererPlayerRender(void *lrp, void *baseCtx,
                                                   void *blockSource,
                                                   void *hitPos) {
  BedrockGame::UpdateFromLevelRendererPlayer(lrp, baseCtx, blockSource);
  if (g_oLevelRendererPlayerRender) {
    g_oLevelRendererPlayerRender(lrp, baseCtx, blockSource, hitPos);
  }
}

static void __fastcall hkRenderLevel(void *levelRender, void *scn, void *a3) {
  g_LastRenderLevelMs.store(GetTickCount64());
  if (g_oRenderLevel) {
    g_oRenderLevel(levelRender, scn, a3);
  }
  BedrockGame::UpdateFromRenderLevel(levelRender, scn);
  Bedrockmetica::CaptureManager::TickWorldUpdate();
  Bedrockmetica::CaptureManager::RenderWorldVisuals();
}

static bool IsBlockingScreenLayer(const char *layer) {
  return strcmp(layer, "chat_screen") == 0 ||
         strcmp(layer, "pause_screen") == 0 ||
         strcmp(layer, "start_screen") == 0 ||
         strcmp(layer, "play_screen") == 0 ||
         strcmp(layer, "inventory_screen") == 0 ||
         strcmp(layer, "settings_screen") == 0 ||
         strstr(layer, "container") != nullptr ||
         strstr(layer, "chest") != nullptr ||
         strstr(layer, "crafting") != nullptr ||
         strstr(layer, "furnace") != nullptr ||
         strstr(layer, "anvil") != nullptr ||
         strstr(layer, "enchant") != nullptr ||
         strstr(layer, "command") != nullptr ||
         strstr(layer, "sign") != nullptr || strstr(layer, "book") != nullptr ||
         strstr(layer, "death") != nullptr ||
         strstr(layer, "store") != nullptr || strstr(layer, "modal") != nullptr;
}

static void __fastcall hkSetupAndRender(void *screenView, void *muirc) {
  static const int vtOff = GET_OFFSET("ScreenView::VisualTree");
  static const int rootOff = GET_OFFSET("VisualTree::root");
  static const int layerOff = GET_OFFSET("UIControl::LayerName");

  char layerBuf[96] = {};
  if (TryReadScreenLayerName(screenView, vtOff, rootOff, layerOff, layerBuf,
                             sizeof(layerBuf))) {
    if (strcmp(layerBuf, "debug_screen") != 0 &&
        strcmp(layerBuf, "toast_screen") != 0) {
      ULONGLONG now = GetTickCount64();
      bool isHud = (strcmp(layerBuf, "hud_screen") == 0 ||
                    strcmp(layerBuf, "f3_screen") == 0 ||
                    strcmp(layerBuf, "zoom_screen") == 0);
      if (isHud) {
        g_LastHudScreenMs.store(now);
      } else if (IsBlockingScreenLayer(layerBuf)) {
        g_LastBlockingScreenMs.store(now);
      }
      g_bInHudScreen.store(isHud);
      g_bHasScreenName.store(true);
      {
        std::lock_guard<std::mutex> lock(g_ScreenNameMtx);
        g_CurrentScreenName = layerBuf;
      }
    }
  }

  if (g_oSetupAndRender) {
    g_oSetupAndRender(screenView, muirc);
  }
}

static void __fastcall hkApplyTurnDelta(void *self, void *delta) {
  g_LastTurnDeltaMs.store(GetTickCount64());
  if (g_bMenuOpen.load()) {
    return;
  }
  if (g_oApplyTurnDelta) {
    g_oApplyTurnDelta(self, delta);
  }
}

static void ProcessMouseInputVectorOnMenuOpen() {
  if (!g_pMouseInputVector)
    return;
  __try {
    g_pMouseInputVector->clear();
    if (g_bNeedReleaseMousePackets.exchange(false)) {
      MouseInputPacket relLeft{};
      relLeft.type = 1;
      relLeft.state = 0;
      g_pMouseInputVector->push_back(relLeft);

      MouseInputPacket relRight{};
      relRight.type = 2;
      relRight.state = 0;
      g_pMouseInputVector->push_back(relRight);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

static void *__fastcall hkInputHandlerTick(void *self, void *a2, void *a3,
                                           void *a4) {
  if (g_bMenuOpen.load()) {
    ProcessMouseInputVectorOnMenuOpen();
  } else {
    g_bNeedReleaseMousePackets.store(false);
  }
  return g_oInputHandlerTick ? g_oInputHandlerTick(self, a2, a3, a4) : nullptr;
}

static void __fastcall hkMouseFeed(void *dev, char button, char action, short x,
                                   short y, short dx, short dy, char a8) {
  if (dev) {
    g_pMouseDevice.store(dev);
  }
  if (g_bMenuOpen.load()) {
    if (!g_bLoggedFeedBlock.exchange(true)) {
      Logger::info("Blocked MouseDevice::feed while menu open.", "Input");
    }
    if (action == 0 && (button == 1 || button == 2) && g_oMouseFeed) {
      g_oMouseFeed(dev, button, action, x, y, 0, 0, a8);
    }
    return;
  }
  if (!g_bMenuOpen.load() && IsGameplayHud() &&
      Bedrockmetica::CaptureManager::IsActive() && button == 2 && action == 1) {
    if (Bedrockmetica::CaptureManager::HandleBareHandRightClick()) {
      return;
    }
  }
  if (g_oMouseFeed) {
    g_oMouseFeed(dev, button, action, x, y, dx, dy, a8);
  }
}

static void *__fastcall hkGameCoreMouse(void *self, void *a2, void *a3,
                                        void *a4) {
  if (g_bMenuOpen.load()) {
    return nullptr;
  }
  return g_oGameCoreMouse ? g_oGameCoreMouse(self, a2, a3, a4) : nullptr;
}

static void *BlockGameCoreResult(void *outResult) {
  if (outResult) {
    __try {
      *reinterpret_cast<uint64_t *>(outResult) = 0;
      *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(outResult) + 8) =
          0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return outResult;
  }
  return nullptr;
}

static void *__fastcall hkGameCorePointer(void *self, void *outResult,
                                          void *arg2) {
  if (g_bMenuOpen.load()) {
    if (!g_bLoggedPtrBlock.exchange(true)) {
      Logger::info("Blocked GameCore::pointerHandler while menu open.",
                   "Input");
    }
    return BlockGameCoreResult(outResult);
  }
  return g_oGameCorePointer ? g_oGameCorePointer(self, outResult, arg2)
                            : outResult;
}

static void *__fastcall hkGameCoreRawMouse(void *self, void *outResult,
                                           void *arg2) {
  if (g_bMenuOpen.load()) {
    if (!g_bLoggedRawBlock.exchange(true)) {
      Logger::info("Blocked GameCore::rawMouseHandler while menu open.",
                   "Input");
    }
    return BlockGameCoreResult(outResult);
  }
  return g_oGameCoreRawMouse ? g_oGameCoreRawMouse(self, outResult, arg2)
                             : outResult;
}

static void *__fastcall hkGameCoreMouseBtn(void *self, void *outResult,
                                           void *arg2) {
  if (g_bMenuOpen.load()) {
    if (!g_bLoggedBtnBlock.exchange(true)) {
      Logger::info("Blocked GameCore::mouseBtnHandler while menu open.",
                   "Input");
    }
    return BlockGameCoreResult(outResult);
  }
  return g_oGameCoreMouseBtn ? g_oGameCoreMouseBtn(self, outResult, arg2)
                             : outResult;
}

static void *__fastcall hkGameCoreMouseMove(void *self, void *outResult,
                                            void *arg2) {
  if (g_bMenuOpen.load()) {
    if (!g_bLoggedMoveBlock.exchange(true)) {
      Logger::info("Blocked GameCore::mouseMoveHandler while menu open.",
                   "Input");
    }
    return BlockGameCoreResult(outResult);
  }
  return g_oGameCoreMouseMove ? g_oGameCoreMouseMove(self, outResult, arg2)
                              : outResult;
}

static void __fastcall hkGrabMouse(void *xwin) {
  if (xwin) {
    g_pXWindow.store(xwin);
  }
  if (g_bMenuOpen.load()) {
    Logger::info("Blocked GameCore::grabMouse while menu open.", "Input");
    return;
  }
  if (g_oGrabMouse) {
    g_oGrabMouse(xwin);
  }
}

static void __fastcall hkReleaseMouse(void *xwin) {
  if (xwin) {
    g_pXWindow.store(xwin);
  }
  if (g_oReleaseMouse) {
    g_oReleaseMouse(xwin);
  }
}

static void InstallGameHooksOnce() {
  static std::atomic<bool> s_GameHooksInstalled{false};
  if (s_GameHooksInstalled.exchange(true))
    return;

  uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  auto tryHook = [&](const char *label, uintptr_t addr, void *detour,
                     void **orig) -> bool {
    if (!addr) {
      Logger::warn(std::string("Hook address missing for ") + label, "Hooks");
      return false;
    }
    MH_STATUS st = MH_CreateHook(reinterpret_cast<void *>(addr), detour, orig);
    if (st == MH_OK || st == MH_ERROR_ALREADY_CREATED) {
      MH_EnableHook(reinterpret_cast<void *>(addr));
      std::ostringstream ss;
      ss << "Hooked " << label << " at RVA 0x" << std::hex << (addr - exeBase);
      Logger::info(ss.str(), "Hooks");
      return true;
    }
    Logger::warn(std::string("Failed hooking ") + label, "Hooks");
    return false;
  };

  BedrockGame::Initialize();

  uintptr_t renderLevelAddr = GET_SIG_ADDRESS("LevelRenderer::renderLevel");
  g_bRenderLevelHooked.store(
      tryHook("LevelRenderer::renderLevel", renderLevelAddr,
              reinterpret_cast<void *>(hkRenderLevel),
              reinterpret_cast<void **>(&g_oRenderLevel)));

  uintptr_t lrpRenderAddr = GET_SIG_ADDRESS("LevelRendererPlayer::render");
  if (lrpRenderAddr) {
    tryHook("LevelRendererPlayer::render", lrpRenderAddr,
            reinterpret_cast<void *>(hkLevelRendererPlayerRender),
            reinterpret_cast<void **>(&g_oLevelRendererPlayerRender));
  }

  uintptr_t setupRenderAddr = GET_SIG_ADDRESS("ScreenView::setupAndRender");
  if (!setupRenderAddr)
    setupRenderAddr = GET_SIG_ADDRESS("ScreenView::setupAndRender_alt");
  if (!setupRenderAddr)
    setupRenderAddr = GET_SIG_ADDRESS("ScreenView::setupAndRender_alt2");
  g_bScreenHooked.store(tryHook("ScreenView::setupAndRender", setupRenderAddr,
                                reinterpret_cast<void *>(hkSetupAndRender),
                                reinterpret_cast<void **>(&g_oSetupAndRender)));

  uintptr_t turnDeltaAddr = GET_SIG_ADDRESS("LocalPlayer::applyTurnDelta");
  tryHook("LocalPlayer::applyTurnDelta", turnDeltaAddr,
          reinterpret_cast<void *>(hkApplyTurnDelta),
          reinterpret_cast<void **>(&g_oApplyTurnDelta));

  uintptr_t inputTickCall = GET_SIG_ADDRESS("InputHandler::tick");
  if (!inputTickCall)
    inputTickCall = GET_SIG_ADDRESS("InputHandler::tick_alt");
  uintptr_t inputTickTarget =
      inputTickCall ? Memory::offsetFromSig(inputTickCall, 1) : 0;

  uintptr_t mouseVecSig = GET_SIG_ADDRESS("MouseInputVector");
  if (mouseVecSig) {
    uintptr_t vecAddr = Memory::offsetFromSig(mouseVecSig, 3);
    if (IsValidPtrRange(vecAddr)) {
      g_pMouseInputVector =
          reinterpret_cast<std::vector<MouseInputPacket> *>(vecAddr);
      Logger::info("Resolved MouseInputVector cleanly.", "Hooks");
    }
  }

  tryHook("InputHandler::tick", inputTickTarget,
          reinterpret_cast<void *>(hkInputHandlerTick),
          reinterpret_cast<void **>(&g_oInputHandlerTick));

  uintptr_t mouseFeedAddr = GET_SIG_ADDRESS("MouseDevice::feed_126");
  if (!mouseFeedAddr) {
    uintptr_t mouseFeedCall = GET_SIG_ADDRESS("MouseDevice::feed");
    if (mouseFeedCall) {
      mouseFeedAddr = Memory::offsetFromSig(mouseFeedCall, 1);
    }
  }
  if (mouseFeedAddr) {
    tryHook("MouseDevice::feed", mouseFeedAddr,
            reinterpret_cast<void *>(hkMouseFeed),
            reinterpret_cast<void **>(&g_oMouseFeed));
  }

  uintptr_t gameCoreMouseAddr = GET_SIG_ADDRESS("GameCore_handleMouseInput");
  if (gameCoreMouseAddr) {
    tryHook("GameCore_handleMouseInput", gameCoreMouseAddr,
            reinterpret_cast<void *>(hkGameCoreMouse),
            reinterpret_cast<void **>(&g_oGameCoreMouse));
  }

  uintptr_t ptrHandlerAddr = GET_SIG_ADDRESS("GameCore::pointerHandler");
  if (ptrHandlerAddr) {
    tryHook("GameCore::pointerHandler", ptrHandlerAddr,
            reinterpret_cast<void *>(hkGameCorePointer),
            reinterpret_cast<void **>(&g_oGameCorePointer));
  }

  uintptr_t rawMouseAddr = GET_SIG_ADDRESS("GameCore::rawMouseHandler");
  if (rawMouseAddr) {
    tryHook("GameCore::rawMouseHandler", rawMouseAddr,
            reinterpret_cast<void *>(hkGameCoreRawMouse),
            reinterpret_cast<void **>(&g_oGameCoreRawMouse));
  }

  uintptr_t mouseBtnAddr = GET_SIG_ADDRESS("GameCore::mouseBtnHandler");
  if (mouseBtnAddr) {
    tryHook("GameCore::mouseBtnHandler", mouseBtnAddr,
            reinterpret_cast<void *>(hkGameCoreMouseBtn),
            reinterpret_cast<void **>(&g_oGameCoreMouseBtn));
  }

  uintptr_t mouseMoveAddr = GET_SIG_ADDRESS("GameCore::mouseMoveHandler");
  if (mouseMoveAddr) {
    tryHook("GameCore::mouseMoveHandler", mouseMoveAddr,
            reinterpret_cast<void *>(hkGameCoreMouseMove),
            reinterpret_cast<void **>(&g_oGameCoreMouseMove));
  }

  uintptr_t grabMouseAddr = GET_SIG_ADDRESS("GameCore::grabMouse");
  if (grabMouseAddr) {
    tryHook("GameCore::grabMouse", grabMouseAddr,
            reinterpret_cast<void *>(hkGrabMouse),
            reinterpret_cast<void **>(&g_oGrabMouse));
  }

  uintptr_t releaseMouseAddr = GET_SIG_ADDRESS("GameCore::releaseMouse");
  if (releaseMouseAddr) {
    tryHook("GameCore::releaseMouse", releaseMouseAddr,
            reinterpret_cast<void *>(hkReleaseMouse),
            reinterpret_cast<void **>(&g_oReleaseMouse));
  }
}

static void ClearMinecraftInputs(HWND hWnd) {
  if (!g_oWndProc || !hWnd)
    return;
  static const WPARAM keys[] = {'W',         'A',       'S',       'D',
                                ' ',         VK_SPACE,  VK_SHIFT,  VK_CONTROL,
                                VK_MENU,     VK_LSHIFT, VK_RSHIFT, VK_LCONTROL,
                                VK_RCONTROL, 'M',       'B'};
  for (WPARAM k : keys) {
    CallWindowProcW(g_oWndProc, hWnd, WM_KEYUP, k, 0xC0000001);
  }
  CallWindowProcW(g_oWndProc, hWnd, WM_LBUTTONUP, 0, 0);
  CallWindowProcW(g_oWndProc, hWnd, WM_RBUTTONUP, 0, 0);
}

static void RecenterAndGrabCursor(HWND hWnd) {
  if (!hWnd)
    return;
  RECT rc{};
  if (!GetClientRect(hWnd, &rc))
    return;
  POINT center{(rc.right + rc.left) / 2, (rc.bottom + rc.top) / 2};
  ClientToScreen(hWnd, &center);
  if (g_oSetCursorPos)
    g_oSetCursorPos(center.x, center.y);
  else
    SetCursorPos(center.x, center.y);

  RECT clip{center.x, center.y, center.x, center.y};
  if (g_oClipCursor)
    g_oClipCursor(&clip);
  else
    ClipCursor(&clip);

  if (g_oSetCapture)
    g_oSetCapture(hWnd);
  else
    SetCapture(hWnd);

  for (int i = 0; i < 8; ++i) {
    int res = g_oShowCursor ? g_oShowCursor(FALSE) : ShowCursor(FALSE);
    if (res < 0)
      break;
  }
}

static void InstallCursorHooksOnce() {
  if (g_bCursorHooksInstalled.exchange(true))
    return;
  HMODULE u32 = GetModuleHandleA("user32.dll");
  if (!u32)
    return;
  auto hookApi = [&](const char *name, void *detour, void **orig) {
    if (void *target = reinterpret_cast<void *>(GetProcAddress(u32, name))) {
      if (MH_CreateHook(target, detour, orig) == MH_OK) {
        MH_EnableHook(target);
      }
    }
  };
  hookApi("ClipCursor", reinterpret_cast<void *>(hkClipCursor),
          reinterpret_cast<void **>(&g_oClipCursor));
  hookApi("SetCursorPos", reinterpret_cast<void *>(hkSetCursorPos),
          reinterpret_cast<void **>(&g_oSetCursorPos));
  hookApi("SetCapture", reinterpret_cast<void *>(hkSetCapture),
          reinterpret_cast<void **>(&g_oSetCapture));
  hookApi("ShowCursor", reinterpret_cast<void *>(hkShowCursor),
          reinterpret_cast<void **>(&g_oShowCursor));
  hookApi("GetCursorPos", reinterpret_cast<void *>(hkGetCursorPos),
          reinterpret_cast<void **>(&g_oGetCursorPos));
  hookApi("GetAsyncKeyState", reinterpret_cast<void *>(hkGetAsyncKeyState),
          reinterpret_cast<void **>(&g_oGetAsyncKeyState));
  hookApi("GetKeyState", reinterpret_cast<void *>(hkGetKeyState),
          reinterpret_cast<void **>(&g_oGetKeyState));
  hookApi("GetRawInputData", reinterpret_cast<void *>(hkGetRawInputData),
          reinterpret_cast<void **>(&g_oGetRawInputData));
  hookApi("GetRawInputBuffer", reinterpret_cast<void *>(hkGetRawInputBuffer),
          reinterpret_cast<void **>(&g_oGetRawInputBuffer));
  hookApi("PeekMessageW", reinterpret_cast<void *>(hkPeekMessageW),
          reinterpret_cast<void **>(&g_oPeekMessageW));
  hookApi("GetPointerType", reinterpret_cast<void *>(hkGetPointerType),
          reinterpret_cast<void **>(&g_oGetPointerType));
}

bool IsInitialized() { return g_bInitialized.load(); }
GraphicsAPI GetCurrentAPI() { return g_CurrentAPI; }
const char *GetCurrentAPIName() {
  if (g_CurrentAPI == GraphicsAPI::D3D11)
    return "DirectX 11";
  if (g_CurrentAPI == GraphicsAPI::D3D12)
    return "DirectX 12";
  return "Unknown";
}
HWND GetGameHwnd() { return g_hGameWindow; }

std::string GetCurrentScreenName() {
  std::lock_guard<std::mutex> lock(g_ScreenNameMtx);
  return g_CurrentScreenName;
}

bool IsInWorld() {
  ULONGLONG now = GetTickCount64();
  if (g_bRenderLevelHooked.load()) {
    ULONGLONG lastMs = g_LastRenderLevelMs.load();
    if (lastMs == 0)
      return false;
    return (now - lastMs) < 550ULL;
  }
  if (g_bHasScreenName.load()) {
    std::string s = GetCurrentScreenName();
    return s != "start_screen" && s != "play_screen" &&
           s != "world_loading_progress_screen";
  }
  return g_bGameClipLocked.load() || g_bGameCursorHidden.load() ||
         (now - g_LastClipLockMs.load()) < 1000ULL ||
         (now - g_LastSetCursorPosMs.load()) < 1000ULL ||
         (now - g_LastTurnDeltaMs.load()) < 5000ULL;
}

bool IsGameplayHud() {
  if (!IsInWorld())
    return false;
  ULONGLONG now = GetTickCount64();

  if (g_bHasScreenName.load()) {
    ULONGLONG hudMs = g_LastHudScreenMs.load();
    ULONGLONG blkMs = g_LastBlockingScreenMs.load();
    if (blkMs != 0 && (now - blkMs) < 350ULL && blkMs >= hudMs) {
      return false;
    }
    if (hudMs != 0 && (now - hudMs) < 550ULL) {
      return true;
    }
    return g_bInHudScreen.load();
  }

  if (g_bGameClipLocked.load() || g_bGameCursorHidden.load()) {
    return true;
  }
  if ((now - g_LastClipLockMs.load()) < 1000ULL ||
      (now - g_LastSetCursorPosMs.load()) < 1000ULL ||
      (now - g_LastShowCursorFalseMs.load()) < 1000ULL) {
    return true;
  }

  CURSORINFO ci{};
  ci.cbSize = sizeof(ci);
  if (GetCursorInfo(&ci)) {
    if ((ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == nullptr) {
      return true;
    }
  }

  return g_bRenderLevelHooked.load();
}

bool IsMenuOpen() { return g_bMenuOpen.load(); }

int GetMenuToggleKey() { return g_MenuToggleKey; }

void SetMenuToggleKey(int vk) {
  if (vk > 0 && vk < 255) {
    g_MenuToggleKey = vk;
    g_MenuToggleKeyVal = vk;
  }
}

std::string GetMenuToggleKeyName() {
  int vk = g_MenuToggleKey;
  switch (vk) {
  case VK_RSHIFT:
    return "RSHIFT";
  case VK_LSHIFT:
    return "LSHIFT";
  case VK_SHIFT:
    return "SHIFT";
  case VK_RCONTROL:
    return "RCTRL";
  case VK_LCONTROL:
    return "LCTRL";
  case VK_CONTROL:
    return "CTRL";
  case VK_TAB:
    return "TAB";
  case VK_INSERT:
    return "INSERT";
  case VK_DELETE:
    return "DELETE";
  case VK_HOME:
    return "HOME";
  case VK_END:
    return "END";
  case VK_PRIOR:
    return "PGUP";
  case VK_NEXT:
    return "PGDN";
  case VK_OEM_3:
    return "TILDE";
  default:
    break;
  }
  if (vk >= 'A' && vk <= 'Z') {
    return std::string(1, static_cast<char>(vk));
  }
  if (vk >= '0' && vk <= '9') {
    return std::string(1, static_cast<char>(vk));
  }
  if (vk >= VK_F1 && vk <= VK_F12) {
    return "F" + std::to_string(vk - VK_F1 + 1);
  }
  UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
  wchar_t wbuf[32] = {};
  if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), wbuf, 32) > 0) {
    std::string out;
    for (int i = 0; wbuf[i] && i < 16; ++i) {
      if (wbuf[i] >= 0x20 && wbuf[i] <= 0x7E) {
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(wbuf[i]))));
      }
    }
    if (!out.empty())
      return out;
  }
  return "KEY";
}

void BeginKeybindCapture() { g_bCapturingKeybind.store(true); }
void CancelKeybindCapture() { g_bCapturingKeybind.store(false); }
bool IsCapturingKeybind() { return g_bCapturingKeybind.load(); }

static void SyncCursorOnWindowThread(HWND hWnd, bool menuOpen) {
  OverlayCallScope scope;
  void *xwin = g_pXWindow.load();
  if (menuOpen) {
    if (xwin && g_oReleaseMouse) {
      g_oReleaseMouse(xwin);
    }
    if (g_oClipCursor)
      g_oClipCursor(nullptr);
    else
      ClipCursor(nullptr);
    ReleaseCapture();
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    for (int i = 0; i < 8; ++i) {
      int res = g_oShowCursor ? g_oShowCursor(TRUE) : ShowCursor(TRUE);
      if (res >= 0)
        break;
    }
  } else {
    if (xwin && g_oGrabMouse) {
      g_oGrabMouse(xwin);
    } else {
      RecenterAndGrabCursor(hWnd);
    }
  }
}

void SetMenuOpen(bool open) {
  if (open && !IsGameplayHud()) {
    return;
  }
  if (g_bMenuOpen.load() == open)
    return;

  Logger::info(std::string("SetMenuOpen -> ") + (open ? "OPEN" : "CLOSE"),
               "Menu");

  if (open) {
    g_bLoggedPtrBlock.store(false);
    g_bLoggedRawBlock.store(false);
    g_bLoggedBtnBlock.store(false);
    g_bLoggedMoveBlock.store(false);
    g_bLoggedFeedBlock.store(false);
    if (g_hGameWindow) {
      OverlayCallScope scope;
      POINT pt{};
      if (!(g_oGetCursorPos ? g_oGetCursorPos(&pt) : GetCursorPos(&pt))) {
        RECT rc{};
        GetClientRect(g_hGameWindow, &rc);
        pt = {(rc.right + rc.left) / 2, (rc.bottom + rc.top) / 2};
        ClientToScreen(g_hGameWindow, &pt);
      }
      g_FrozenCursorPos = pt;
    }
  } else {
    g_bCapturingKeybind.store(false);
  }
  g_bMenuOpen.store(open);

  bool onWinThread =
      g_hGameWindow && (GetCurrentThreadId() ==
                        GetWindowThreadProcessId(g_hGameWindow, nullptr));

  if (open) {
    g_bNeedReleaseMousePackets.store(true);
    if (g_pMouseDevice.load() && g_oMouseFeed) {
      g_oMouseFeed(g_pMouseDevice.load(), 1, 0, 0, 0, 0, 0, 0);
      g_oMouseFeed(g_pMouseDevice.load(), 2, 0, 0, 0, 0, 0, 0);
    }
    ClearMinecraftInputs(g_hGameWindow);
    if (onWinThread) {
      SyncCursorOnWindowThread(g_hGameWindow, true);
    } else if (g_hGameWindow) {
      PostMessageW(g_hGameWindow, WM_APP_SYNC_CURSOR, 1, 0);
    }
    SoundManager::Play(UISound::OpenMenu);
  } else {
    {
      OverlayCallScope scope;
      if (ImGui::GetCurrentContext()) {
        ImGui::SetWindowFocus(nullptr);
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
      }
    }
    if (IsGameplayHud()) {
      if (onWinThread) {
        SyncCursorOnWindowThread(g_hGameWindow, false);
      } else if (g_hGameWindow) {
        PostMessageW(g_hGameWindow, WM_APP_SYNC_CURSOR, 0, 0);
      }
    }
    SoundManager::Play(UISound::CloseMenu);
  }
}

void ToggleMenu() {
  if (IsMenuOpen()) {
    SetMenuOpen(false);
  } else if (IsGameplayHud()) {
    SetMenuOpen(true);
  }
}

ImFont *GetFontRegular() { return g_pFontRegular; }
ImFont *GetFontBold() { return g_pFontBold; }
ImFont *GetFontMinecraft() { return g_pFontMinecraft; }

static LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam) {
  if (uMsg == WM_APP_SYNC_CURSOR) {
    SyncCursorOnWindowThread(hWnd, wParam != 0);
    return 0;
  }

  if (uMsg == WM_SETCURSOR && !g_bMenuOpen.load()) {
    if (g_bNeedClearCursorAfterClose.exchange(false)) {
      if (IsGameplayHud()) {
        SetCursor(nullptr);
        return TRUE;
      }
    }
  }

  if (uMsg == WM_KILLFOCUS) {
    g_bCapturingKeybind.store(false);
    if (g_bMenuOpen.load()) {
      SetMenuOpen(false);
    }
    return CallWindowProcW(g_oWndProc, hWnd, uMsg, wParam, lParam);
  }

  if (g_bMenuOpen.load() && g_bCapturingKeybind.load() &&
      (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) &&
      (lParam & (1LL << 30)) == 0) {
    int vk = static_cast<int>(wParam);
    if (vk == VK_ESCAPE) {
      g_bCapturingKeybind.store(false);
      return 0;
    }
    if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON &&
        vk != VK_RETURN) {
      if (vk == VK_SHIFT) {
        UINT sc = (static_cast<UINT>(lParam) >> 16) & 0xFF;
        int mapped = static_cast<int>(MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX));
        if (mapped == VK_RSHIFT || mapped == VK_LSHIFT)
          vk = mapped;
      }
      SetMenuToggleKey(vk);
      ConfigManager::SaveMenuToggleKey(vk);
      g_bCapturingKeybind.store(false);
      std::string kn = GetMenuToggleKeyName();
      SoundManager::Play(UISound::Success);
      ToastManager::Show(ToastType::Success, "KEYBIND UPDATED",
                         "Menu toggle key set to [" + kn + "]. Press [" + kn +
                             "] anytime in-world.",
                         "[" + kn + "]");
      return 0;
    }
  }

  if (uMsg == WM_KEYDOWN && wParam == static_cast<WPARAM>(g_MenuToggleKey) &&
      (lParam & (1LL << 30)) == 0) {
    if (g_bMenuOpen.load()) {
      if (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantTextInput) {
        SetMenuOpen(false);
        return 0;
      }
    } else if (IsGameplayHud()) {
      SetMenuOpen(true);
      return 0;
    }
  }

  if (!g_bMenuOpen.load() && IsGameplayHud() && uMsg == WM_KEYDOWN &&
      wParam == 'B' && (lParam & (1LL << 30)) == 0) {
    bool next = !SchematicMenu::IsCaptureModeActive();
    SchematicMenu::SetCaptureModeActive(next);
    std::string kn = GetMenuToggleKeyName();
    ToastManager::Show(
        next ? ToastType::Info : ToastType::Warning, "CAPTURE MODE",
        next ? ("Capture mode enabled! Press [" + kn + "] to manage builds.")
             : "Capture mode disabled.",
        "[" + kn + "]");
    return 0;
  }

  if (!g_bMenuOpen.load() && IsGameplayHud() &&
      SchematicMenu::IsCaptureModeActive() && uMsg == WM_RBUTTONDOWN) {
    if (BedrockGame::IsLocalPlayerBareHanded()) {
      return 0;
    }
  }

  if (g_bMenuOpen.load()) {
    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE &&
        (lParam & (1LL << 30)) == 0) {
      if (SchematicMenu::HasOpenModal()) {
        SchematicMenu::CloseActiveModal();
      } else {
        SetMenuOpen(false);
      }
      return 0;
    }

    if (uMsg == WM_SETCURSOR) {
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      return TRUE;
    }

    if (uMsg == WM_INPUT) {
      return 0;
    }

    {
      OverlayCallScope scope;
      ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    }

    if ((uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) ||
        (uMsg >= WM_NCMOUSEMOVE && uMsg <= WM_NCMBUTTONDBLCLK) ||
        (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST)) {
      return 0;
    }
  }

  return CallWindowProcW(g_oWndProc, hWnd, uMsg, wParam, lParam);
}

static void InstallWndProcHook(HWND hWnd) {
  if (!g_oWndProc && hWnd) {
    g_hGameWindow = hWnd;
    g_oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));
  }
}

static void RemoveWndProcHook() {
  if (g_oWndProc && g_hGameWindow) {
    SetWindowLongPtrW(g_hGameWindow, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(g_oWndProc));
    g_oWndProc = nullptr;
    g_hGameWindow = nullptr;
  }
}

static GraphicsAPI DetectAPI(IDXGISwapChain *pSwapChain) {
  if (!pSwapChain)
    return GraphicsAPI::Unknown;
  ID3D12Device *d12 = nullptr;
  if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device),
                                      reinterpret_cast<void **>(&d12)))) {
    d12->Release();
    return GraphicsAPI::D3D12;
  }
  ID3D11Device *d11 = nullptr;
  if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device),
                                      reinterpret_cast<void **>(&d11)))) {
    d11->Release();
    return GraphicsAPI::D3D11;
  }
  return GraphicsAPI::Unknown;
}

static void CreateD3D11RenderTarget(IDXGISwapChain *pSwapChain) {
  ID3D11Texture2D *backBuf = nullptr;
  if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void **>(&backBuf)))) {
    g_pd3d11Device->CreateRenderTargetView(backBuf, nullptr, &g_pd3d11RTV);
    backBuf->Release();
  }
}

static void ReleaseD3D11RenderTarget() {
  if (g_pd3d11RTV) {
    g_pd3d11RTV->Release();
    g_pd3d11RTV = nullptr;
  }
}

static void WaitForGpuD3D12() {
  if (!g_pd3d12CommandQueue || !g_pd3d12Fence || !g_d3d12FenceEvent)
    return;
  UINT64 val = ++g_d3d12FenceCounter;
  if (SUCCEEDED(g_pd3d12CommandQueue->Signal(g_pd3d12Fence, val))) {
    if (g_pd3d12Fence->GetCompletedValue() < val) {
      g_pd3d12Fence->SetEventOnCompletion(val, g_d3d12FenceEvent);
      WaitForSingleObject(g_d3d12FenceEvent, 2000);
    }
  }
}

static void CreateD3D12RenderTargets(IDXGISwapChain *pSwapChain) {
  DXGI_SWAP_CHAIN_DESC desc;
  pSwapChain->GetDesc(&desc);
  g_d3d12BufferCount = desc.BufferCount;

  g_d3d12RenderTargets.resize(g_d3d12BufferCount, nullptr);
  g_d3d12RtvHandles.resize(g_d3d12BufferCount);

  SIZE_T rtvInc = g_pd3d12Device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_pd3d12RtvDescHeap->GetCPUDescriptorHandleForHeapStart();

  for (UINT i = 0; i < g_d3d12BufferCount; ++i) {
    g_d3d12RtvHandles[i] = rtvHandle;
    rtvHandle.ptr += rtvInc;
    if (SUCCEEDED(pSwapChain->GetBuffer(
            i, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&g_d3d12RenderTargets[i])))) {
      g_pd3d12Device->CreateRenderTargetView(g_d3d12RenderTargets[i], nullptr,
                                             g_d3d12RtvHandles[i]);
    }
  }
}

static void ReleaseD3D12RenderTargets() {
  WaitForGpuD3D12();
  for (auto *&rt : g_d3d12RenderTargets) {
    if (rt) {
      rt->Release();
      rt = nullptr;
    }
  }
  g_d3d12RenderTargets.clear();
  g_d3d12RtvHandles.clear();
}

static void LoadMenuFonts(ImGuiIO &io) {
  HMODULE hMod = GetModuleHandleA("Bedrockmetica.dll");

  ImFontConfig smoothCfg;
  smoothCfg.OversampleH = 3;
  smoothCfg.OversampleV = 2;
  smoothCfg.PixelSnapH = false;

  ImFontConfig pixelCfg;
  pixelCfg.OversampleH = 2;
  pixelCfg.OversampleV = 2;
  pixelCfg.PixelSnapH = true;

  std::string regPath = Utils::ResolveFontPath(hMod, "Poppins-Medium.ttf");
  if (regPath.empty())
    regPath = Utils::ResolveFontPath(hMod, "font.ttf");

  std::string boldPath = Utils::ResolveFontPath(hMod, "Poppins-SemiBold.ttf");
  if (boldPath.empty())
    boldPath = Utils::ResolveFontPath(hMod, "font_bold.ttf");

  std::string fivePath =
      Utils::ResolveFontPath(hMod, "minecraft-five-bold.otf");
  if (fivePath.empty())
    fivePath = Utils::ResolveFontPath(hMod, "minecraftia.ttf");

  if (!regPath.empty())
    g_pFontRegular =
        io.Fonts->AddFontFromFileTTF(regPath.c_str(), 24.0f, &smoothCfg);
  if (!boldPath.empty())
    g_pFontBold =
        io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 26.0f, &smoothCfg);
  if (!fivePath.empty())
    g_pFontMinecraft =
        io.Fonts->AddFontFromFileTTF(fivePath.c_str(), 34.0f, &pixelCfg);

  if (!g_pFontRegular)
    g_pFontRegular = io.Fonts->AddFontDefault();
  if (!g_pFontBold)
    g_pFontBold = g_pFontRegular;
  if (!g_pFontMinecraft)
    g_pFontMinecraft = g_pFontBold;

  Logger::info("3-tier UI fonts (Minecraft Five + Poppins) loaded cleanly.",
               "SwapchainHook");
}

static bool InitImGui(IDXGISwapChain *pSwapChain) {
  DXGI_SWAP_CHAIN_DESC desc;
  pSwapChain->GetDesc(&desc);
  HWND hWnd =
      desc.OutputWindow ? desc.OutputWindow : Utils::FindMinecraftWindow();
  if (!hWnd)
    hWnd = GetForegroundWindow();

  InstallWndProcHook(hWnd);
  InstallCursorHooksOnce();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;

  Theme::ApplyOreUITheme();
  LoadMenuFonts(io);
  ImGui_ImplWin32_Init(hWnd);

  g_CurrentAPI = DetectAPI(pSwapChain);

  if (g_CurrentAPI == GraphicsAPI::D3D11) {
    if (FAILED(pSwapChain->GetDevice(
            __uuidof(ID3D11Device),
            reinterpret_cast<void **>(&g_pd3d11Device)))) {
      return false;
    }
    g_pd3d11Device->GetImmediateContext(&g_pd3d11Context);
    CreateD3D11RenderTarget(pSwapChain);
    ImGui_ImplDX11_Init(g_pd3d11Device, g_pd3d11Context);
    Logger::success("DirectX 11 ImGui backend initialized.", "SwapchainHook");
  } else if (g_CurrentAPI == GraphicsAPI::D3D12) {
    if (FAILED(pSwapChain->GetDevice(
            __uuidof(ID3D12Device),
            reinterpret_cast<void **>(&g_pd3d12Device)))) {
      return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = desc.BufferCount;
    g_pd3d12Device->CreateDescriptorHeap(&rtvDesc,
                                         IID_PPV_ARGS(&g_pd3d12RtvDescHeap));

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = MAX_SRV_DESCRIPTORS;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    g_pd3d12Device->CreateDescriptorHeap(&srvDesc,
                                         IID_PPV_ARGS(&g_pd3d12SrvDescHeap));

    g_d3d12SrvIncrement = g_pd3d12Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_d3d12SrvAllocIndex = 0;

    g_d3d12Allocators.resize(desc.BufferCount, nullptr);
    g_d3d12FrameFenceValues.resize(desc.BufferCount, 0);
    for (UINT i = 0; i < desc.BufferCount; ++i) {
      g_pd3d12Device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_d3d12Allocators[i]));
    }
    g_pd3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      g_d3d12Allocators[0], nullptr,
                                      IID_PPV_ARGS(&g_pd3d12CmdList));
    g_pd3d12CmdList->Close();

    g_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                IID_PPV_ARGS(&g_pd3d12Fence));
    g_d3d12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    CreateD3D12RenderTargets(pSwapChain);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = g_pd3d12Device;
    initInfo.CommandQueue = g_pd3d12CommandQueue;
    initInfo.NumFramesInFlight = desc.BufferCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = g_pd3d12SrvDescHeap;
    initInfo.SrvDescriptorAllocFn = SrvDescriptorAllocImGui;
    initInfo.SrvDescriptorFreeFn = SrvDescriptorFreeImGui;
    ImGui_ImplDX12_Init(&initInfo);

    Logger::success("DirectX 12 ImGui backend initialized.", "SwapchainHook");
  } else {
    return false;
  }

  SchematicMenu::Initialize();
  g_bImGuiInit.store(true);
  return true;
}

static void RenderOverlayFrame(float width, float height) {
  CrashGuard::TryExecute(
      [&]() {
        bool inWorld = IsInWorld();
        if (g_bMenuOpen.load() && !IsGameplayHud()) {
          SetMenuOpen(false);
        }
        bool menuOpen = g_bMenuOpen.load();
        if (inWorld || menuOpen) {
          SchematicMenu::Render(width, height, menuOpen);
        }
        ToastManager::UpdateAndRender(width, height, ImGui::GetIO().DeltaTime);
      },
      "RenderOverlayFrame");
}

static HRESULT __stdcall hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT count,
                                         UINT width, UINT height,
                                         DXGI_FORMAT fmt, UINT flags) {
  if (g_CurrentAPI == GraphicsAPI::D3D11)
    ReleaseD3D11RenderTarget();
  else if (g_CurrentAPI == GraphicsAPI::D3D12)
    ReleaseD3D12RenderTargets();

  HRESULT hr = g_oResizeBuffers ? g_oResizeBuffers(pSwapChain, count, width,
                                                   height, fmt, flags)
                                : S_OK;

  if (g_CurrentAPI == GraphicsAPI::D3D11 && g_pd3d11Device)
    CreateD3D11RenderTarget(pSwapChain);
  else if (g_CurrentAPI == GraphicsAPI::D3D12 && g_pd3d12Device)
    CreateD3D12RenderTargets(pSwapChain);
  return hr;
}

static HRESULT __stdcall hkResizeBuffers1(IDXGISwapChain3 *pSwapChain,
                                          UINT count, UINT width, UINT height,
                                          DXGI_FORMAT fmt, UINT flags,
                                          const UINT *nodeMask,
                                          IUnknown *const *presentQueue) {
  if (g_CurrentAPI == GraphicsAPI::D3D12)
    ReleaseD3D12RenderTargets();
  HRESULT hr = g_oResizeBuffers1
                   ? g_oResizeBuffers1(pSwapChain, count, width, height, fmt,
                                       flags, nodeMask, presentQueue)
                   : S_OK;
  if (g_CurrentAPI == GraphicsAPI::D3D12 && g_pd3d12Device)
    CreateD3D12RenderTargets(pSwapChain);
  return hr;
}

static HRESULT __stdcall hkPresent(IDXGISwapChain *pSwapChain,
                                   UINT syncInterval, UINT flags) {
  if (CrashGuard::IsPanicUnloadRequested()) {
    return g_oPresent(pSwapChain, syncInterval, flags);
  }

  if (g_CurrentAPI == GraphicsAPI::Unknown) {
    g_CurrentAPI = DetectAPI(pSwapChain);
  }
  if (g_CurrentAPI == GraphicsAPI::D3D12 && !g_pd3d12CommandQueue) {
    return g_oPresent(pSwapChain, syncInterval, flags);
  }

  if (!g_bImGuiInit.load() && !InitImGui(pSwapChain)) {
    return g_oPresent(pSwapChain, syncInterval, flags);
  }

  static bool s_ResizeHooked = false;
  if (!s_ResizeHooked) {
    s_ResizeHooked = true;
    void **vtable = *reinterpret_cast<void ***>(pSwapChain);
    if (MH_CreateHook(vtable[13], reinterpret_cast<void *>(hkResizeBuffers),
                      reinterpret_cast<void **>(&g_oResizeBuffers)) == MH_OK) {
      MH_EnableHook(vtable[13]);
    }
    if (g_CurrentAPI == GraphicsAPI::D3D12) {
      IDXGISwapChain3 *sc3 = nullptr;
      if (SUCCEEDED(pSwapChain->QueryInterface(
              __uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&sc3)))) {
        void **vt3 = *reinterpret_cast<void ***>(sc3);
        if (MH_CreateHook(vt3[39], reinterpret_cast<void *>(hkResizeBuffers1),
                          reinterpret_cast<void **>(&g_oResizeBuffers1)) ==
            MH_OK) {
          MH_EnableHook(vt3[39]);
        }
        sc3->Release();
      }
    }
  }

  DXGI_SWAP_CHAIN_DESC desc;
  pSwapChain->GetDesc(&desc);
  float width = static_cast<float>(desc.BufferDesc.Width);
  float height = static_cast<float>(desc.BufferDesc.Height);

  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  ImGui::GetIO().MouseDrawCursor = false;

  if (g_CurrentAPI == GraphicsAPI::D3D11 && g_pd3d11Context && g_pd3d11RTV) {
    UiAssets::EnsureDX11(g_pd3d11Device);

    {
      OverlayCallScope scope;
      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      if (g_bMenuOpen.load() && g_hGameWindow) {
        POINT pt{};
        if (GetCursorPos(&pt) && ScreenToClient(g_hGameWindow, &pt)) {
          ImGui::GetIO().AddMousePosEvent(static_cast<float>(pt.x),
                                          static_cast<float>(pt.y));
        }
      }
    }
    ImGui::NewFrame();

    RenderOverlayFrame(width, height);

    ImGui::Render();
    g_pd3d11Context->OMSetRenderTargets(1, &g_pd3d11RTV, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  } else if (g_CurrentAPI == GraphicsAPI::D3D12 && g_pd3d12CommandQueue &&
             g_pd3d12CmdList) {
    UiAssets::EnsureDX12(g_pd3d12Device, g_pd3d12CommandQueue, AllocD3D12Srv);

    auto *sc3 = reinterpret_cast<IDXGISwapChain3 *>(pSwapChain);
    UINT idx = sc3->GetCurrentBackBufferIndex();
    if (idx >= g_d3d12Allocators.size() || !g_d3d12Allocators[idx] ||
        !g_d3d12RenderTargets[idx]) {
      return g_oPresent(pSwapChain, syncInterval, flags);
    }

    if (g_pd3d12Fence && g_d3d12FenceEvent &&
        g_d3d12FrameFenceValues[idx] != 0 &&
        g_pd3d12Fence->GetCompletedValue() < g_d3d12FrameFenceValues[idx]) {
      g_pd3d12Fence->SetEventOnCompletion(g_d3d12FrameFenceValues[idx],
                                          g_d3d12FenceEvent);
      WaitForSingleObject(g_d3d12FenceEvent, 2000);
    }

    ID3D12CommandAllocator *alloc = g_d3d12Allocators[idx];
    if (FAILED(alloc->Reset())) {
      return g_oPresent(pSwapChain, syncInterval, flags);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_d3d12RenderTargets[idx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    g_pd3d12CmdList->Reset(alloc, nullptr);
    g_pd3d12CmdList->ResourceBarrier(1, &barrier);
    g_pd3d12CmdList->OMSetRenderTargets(1, &g_d3d12RtvHandles[idx], FALSE,
                                        nullptr);
    g_pd3d12CmdList->SetDescriptorHeaps(1, &g_pd3d12SrvDescHeap);

    {
      OverlayCallScope scope;
      ImGui_ImplDX12_NewFrame();
      ImGui_ImplWin32_NewFrame();
      if (g_bMenuOpen.load() && g_hGameWindow) {
        POINT pt{};
        if (GetCursorPos(&pt) && ScreenToClient(g_hGameWindow, &pt)) {
          ImGui::GetIO().AddMousePosEvent(static_cast<float>(pt.x),
                                          static_cast<float>(pt.y));
        }
      }
    }
    ImGui::NewFrame();

    RenderOverlayFrame(width, height);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3d12CmdList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_pd3d12CmdList->ResourceBarrier(1, &barrier);
    g_pd3d12CmdList->Close();

    ID3D12CommandList *cmdLists[] = {g_pd3d12CmdList};
    g_pd3d12CommandQueue->ExecuteCommandLists(1, cmdLists);

    if (g_pd3d12Fence) {
      UINT64 signalVal = ++g_d3d12FenceCounter;
      g_pd3d12CommandQueue->Signal(g_pd3d12Fence, signalVal);
      g_d3d12FrameFenceValues[idx] = signalVal;
    }
  }

  return g_oPresent(pSwapChain, syncInterval, flags);
}

static void __stdcall
hkExecuteCommandLists(ID3D12CommandQueue *pQueue, UINT numCommandLists,
                      ID3D12CommandList *const *ppCommandLists) {
  if (!g_pd3d12CommandQueue && pQueue &&
      pQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
    g_pd3d12CommandQueue = pQueue;
    Logger::info("Captured game direct ID3D12CommandQueue", "SwapchainHook");
  }
  g_oExecuteCommandLists(pQueue, numCommandLists, ppCommandLists);
}

bool Initialize() {
  Logger::info("Initializing Kiero DXGI SwapChain hooks...", "SwapchainHook");

  kiero::Status::Enum status = kiero::init(kiero::RenderType::D3D12);
  if (status != kiero::Status::Success) {
    status = kiero::init(kiero::RenderType::D3D11);
  }
  if (status != kiero::Status::Success) {
    Logger::error("Failed to initialize kiero for DX11/DX12.", "SwapchainHook");
    return false;
  }

  if (kiero::getRenderType() == kiero::RenderType::D3D12) {
    kiero::bind(54, reinterpret_cast<void **>(&g_oExecuteCommandLists),
                reinterpret_cast<void *>(hkExecuteCommandLists));
    kiero::bind(140, reinterpret_cast<void **>(&g_oPresent),
                reinterpret_cast<void *>(hkPresent));
  } else if (kiero::getRenderType() == kiero::RenderType::D3D11) {
    kiero::bind(8, reinterpret_cast<void **>(&g_oPresent),
                reinterpret_cast<void *>(hkPresent));
  }

  InstallGameHooksOnce();
  g_bInitialized.store(true);
  return true;
}

void Shutdown() {
  RemoveWndProcHook();
  MH_DisableHook(MH_ALL_HOOKS);
  UiAssets::Shutdown();

  if (g_bImGuiInit.load()) {
    g_bImGuiInit.store(false);
    if (g_CurrentAPI == GraphicsAPI::D3D11)
      ImGui_ImplDX11_Shutdown();
    else if (g_CurrentAPI == GraphicsAPI::D3D12)
      ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
  }

  ReleaseD3D11RenderTarget();
  if (g_pd3d11Context) {
    g_pd3d11Context->Release();
    g_pd3d11Context = nullptr;
  }
  if (g_pd3d11Device) {
    g_pd3d11Device->Release();
    g_pd3d11Device = nullptr;
  }

  ReleaseD3D12RenderTargets();
  for (auto *a : g_d3d12Allocators)
    if (a)
      a->Release();
  g_d3d12Allocators.clear();
  if (g_pd3d12Fence) {
    g_pd3d12Fence->Release();
    g_pd3d12Fence = nullptr;
  }
  if (g_d3d12FenceEvent) {
    CloseHandle(g_d3d12FenceEvent);
    g_d3d12FenceEvent = nullptr;
  }
  if (g_pd3d12CmdList) {
    g_pd3d12CmdList->Release();
    g_pd3d12CmdList = nullptr;
  }
  if (g_pd3d12RtvDescHeap) {
    g_pd3d12RtvDescHeap->Release();
    g_pd3d12RtvDescHeap = nullptr;
  }
  if (g_pd3d12SrvDescHeap) {
    g_pd3d12SrvDescHeap->Release();
    g_pd3d12SrvDescHeap = nullptr;
  }
  if (g_pd3d12Device) {
    g_pd3d12Device->Release();
    g_pd3d12Device = nullptr;
  }

  kiero::shutdown();
  g_bInitialized.store(false);
}

} // namespace SwapchainHook
