# Bedrockmetica

An in-process C++23 client schematic suite for **Minecraft: Bedrock Edition (`1.26.x` GDK / WinUI 3 Desktop)**, built directly over DirectX 11 / DirectX 12 RenderDragon hooks with a custom 9-slice UI engine, runtime game-asset decoding, and strict raw-input isolation.

> **Status:** Core runtime infrastructure, graphics hooks, input subsystem, asset pipeline, and HUD/Library interface are implemented and compiling cleanly. World-capture, native 3D hologram rendering, offscreen 3D viewport preview, and binary schematic I/O (`.bmet` / `.mcstructure`) are currently in active engineering.

---

## Key Technical Highlights

### 1. Dual-API DX11 / DX12 SwapChain & Command-Queue Interception
- Automatically detects and attaches via **Kiero + MinHook** to `IDXGISwapChain::Present`, `IDXGISwapChain::ResizeBuffers`, `IDXGISwapChain3::ResizeBuffers1`, and `ID3D12CommandQueue::ExecuteCommandLists` (`src/Hooks/SwapchainHook.cpp`).
- Handles **DirectX 12 multi-buffered frames** cleanly: captures the game's live Direct command queue, maintains per-backbuffer command allocators and fence synchronization values, dynamically grows descriptor heaps (`SRV`), and resets/releases backbuffer RTVs safely across resolution switches and fullscreen toggles.
- Includes **WIC + DirectX texture upload pipelines** (`src/UI/UiAssets.cpp`) with explicit GPU upload command-list fencing so runtime textures never hazard in-flight GPU work.

### 2. GDK / WinUI 3 Raw Input & Pointer Isolation
- Modern Windows GDK builds of Bedrock (`1.26.x`) bypass legacy Win32 `WM_MOUSEMOVE` inside gameplay using `GameCore` raw-pointer callbacks and WinRT pointer throttling.
- Hooks `GameCore` pointer dispatch, mouse-grab control, and mouse device feeding alongside a subclassed `WndProc` hook to cleanly capture mouse/keyboard input when overlays are open, freeze camera look during menu interaction, restore relative cursor grab state on exit, and prevent keystroke leakage into gameplay.

### 3. Zero-Asset 9-Slice UI Engine & Runtime `.brarchive` Decoding
- Custom immediate-mode UI layer (`src/UI/UiDraw.cpp`, `src/UI/SchematicMenu.cpp`) built on **Dear ImGui**:
  - Procedural crisp 9-slice beveled panels, animated state buttons, diamond hotkey badges, and animated drawer transitions sized dynamically to viewport DPI (`ui_size` & `text_size` presets).
  - **Runtime `.brarchive` extractor** (`src/UI/UiAssets.cpp`): parses Bedrock's internal `__brarchive/textures/blocks.brarchive` binary containers (`0x267052a0b125277d` magic, 256-byte index entries) directly from disk via Windows PE process path discovery to decode real vanilla block PNG/TGA textures into GPU shader resources on the fly.

### 4. Fault Isolation, Audio Feedback & Persistence
- **SEH CrashGuard (`src/Utils/CrashGuard.cpp`)**: Installs an unhandled exception / vectored filter scoped cleanly to module address ranges, logs diagnostic state, and signals clean hook uninstallation (`FreeLibraryAndExitThread`) on panic unload (`End`).
- **Synthesized PCM Audio (`src/Utils/SoundManager.cpp`)**: Generates envelope-shaped in-memory WAV buffers and dispatches them asynchronously without external audio dependencies, plus live sound/toast queue (`src/Utils/ToastManager.cpp`).
- **Config Persistence (`src/Utils/ConfigManager.hpp`)**: Thread-safe persistent user configuration (`bedrockmetica.cfg`) across DLL-local, `%LOCALAPPDATA%`, and `%TEMP%` paths.

---

## Architecture Overview

```text
Bedrockmetica.dll (DllMain -> MainThread)
 ├── Initialization
 │    ├── CrashGuard (SEH / panic unload monitor)
 │    ├── ConfigManager (load keybinds, opacity, SFX, scale)
 │    ├── SignatureAndOffsetManager (AOB scan + SDK offsets)
 │    └── SoundManager (in-memory WAV synthesis)
 ├── Hooks (SwapchainHook + MinHook + Kiero)
 │    ├── DXGI Present / ResizeBuffers(1) / D3D12 ExecuteCommandLists
 │    ├── WndProc + GameCore pointer & mouse-grab isolation
 │    └── ScreenView::setupAndRender (HUD state detection)
 └── UI & Assets
      ├── UiAssets (WIC loader, runtime .brarchive block atlas parser, GPU SRVs)
      ├── UiDraw (9-slice panel primitives, badges, animations)
      ├── SchematicMenu (Library view + Active Hologram Sidebar + Settings modal)
      └── ToastManager (animated top-right notification stack)
```

---

## Directory Layout

```text
├── CMakeLists.txt
├── src/
│   ├── dllmain.cpp                      # Entry point, lifecycle & panic-unload thread
│   ├── Assets/Fonts/                    # UI & pixel typography
│   ├── Hooks/                           # SwapChain DX11/DX12, WndProc & GDK input hooks
│   ├── SDK/                             # Bedrock ECS components, camera, block & level types
│   ├── UI/                              # 9-slice primitives, runtime .brarchive loader & menus
│   └── Utils/                           # AOB scanner, CrashGuard, audio, config & toasts
└── ext/                                 # Vendored dependencies (ImGui, Kiero, MinHook, GLM)
```

---

## Building from Source

### Requirements
- Windows 10/11 (x64)
- Visual Studio 2022+ (MSVC v143 / C++23)
- CMake 3.24+
- Windows 10/11 SDK (`d3d11`, `d3d12`, `dxgi`, `d3dcompiler`, `windowscodecs`)

### Build Steps
```powershell
git clone https://github.com/VarunKulkarni15/Bedrockmetica.git
cd Bedrockmetica

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The compiled binary will be output at:
`build/Release/Bedrockmetica.dll`

---

## Default Controls

| Key | Action |
| :--- | :--- |
| `M` (configurable) | Toggle Schematic Library / Active Hologram Menu |
| `B` | Toggle World Selection Capture Mode |
| `Esc` | Close active modal or menu |
| `End` | Safely unhook and unload `Bedrockmetica.dll` |

---

## Roadmap

- [ ] **Phase 1 — Capture & `.bmet` v2 Persistence:** Updated `1.26.x` `LevelRenderer` / `BlockSource` signatures, right-click Pos1/Pos2 bare-hand world capture, paletted sparse-subchunk binary format (`.bmet`).
- [ ] **Phase 2 — Structure Serialization:** Full `.mcstructure` (Little-Endian NBT v1/v2 `IntArrayTag`) import/export + block-state/material palette resolver.
- [ ] **Phase 3 — Offscreen 3D Viewport Preview:** Dirty-on-demand D3D12/D3D11 RTT structure preview inside Screen 1 with greedy face culling & bounding-sphere auto-camera framing.
- [ ] **Phase 4 — Native Hologram Renderer:** Depth-tested ghost block rendering inside Bedrock's render pass with chunk-radius streaming, nudge/rotate/mirror controls, Y-layer slicing, and live block diff verification.

---

## Third-Party Libraries

Bedrockmetica uses the following open-source libraries under their respective licenses inside `ext/`:
- [Dear ImGui](https://github.com/ocornut/imgui) — MIT License
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD 2-Clause License
- [Kiero](https://github.com/Rebzzel/kiero) — MIT License (with D3D11/D3D12/MinHook flags enabled in `kiero.h`)
- [GLM](https://github.com/g-truc/glm) — Happy Bunny / MIT License

---

## Disclaimer

This software interacts directly with local client memory and rendering APIs of Minecraft: Bedrock Edition. It is intended for educational systems/graphics programming research and creative building workflows on permitted worlds/servers.

## License

Licensed under the [MIT License](LICENSE).
