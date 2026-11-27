# Bedrockmetica — Client State & Architecture Summary

**Last Updated:** 2026-11-27  
**Target Binary:** Minecraft Bedrock Edition `1.26.6501.0` (Windows x64)  
**Repository:** `https://github.com/VarunKulkarni15/Bedrockmetica.git` (branch `main`, latest pushed commit `e088609`)  
**Workspace:** `c:\Users\varun\OneDrive\Desktop\projects\bedrockmetica`  
**Build Output:** `build\Release\Bedrockmetica.dll`

> **Uncommitted Work-in-Progress on Disk (paused cleanly at user request):**
> - `src/UI/BlockTextureRegistry.hpp` & `src/UI/BlockTextureRegistry.cpp`: Added `SyncGpuDX11` / `SyncGpuDX12` on-demand GPU atlas re-upload (`D3D11_USAGE_DEFAULT` + persistent `g_Dx12UploadBuf`), `g_AtlasDirty` tracking, `short_grass`/`tall_grass`/`tallgrass` tint fixes, and `dirt_path`/`iron_chain` aliases.
> - `src/UI/PreviewRenderer.cpp`: Not yet updated with the `SyncGpuDX11`/`SyncGpuDX12` call or the `0..16` pixel cuboid + per-face sub-UV engine (`emitCuboid`) — ready to resume right after compaction.

---

## 1. Client Architecture

`Bedrockmetica` is an injected x64 C++20 internal DLL (`kiero` + `MinHook` + `ImGui` + `D3D11/D3D12` + native Bedrock engine hooks) organized into 5 layers:

1. **Entry & Hook Layer (`src/dllmain.cpp`, `src/Hooks/SwapchainHook.cpp`)**
   - Detects active graphics API via `kiero` (`D3D11` or `D3D12`, `BufferCount = 3`, `DXGI_FORMAT_R8G8B8A8_UNORM`).
   - Hooks `IDXGISwapChain::Present` (`hkPresent`), `ResizeBuffers`, `ExecuteCommandLists` (to capture D3D12 command queue), `WndProc` + raw mouse/pointer handlers so ImGui intercepts input cleanly when the overlay is open (`INSERT` / menu toggle) or capture hotkeys fire (`B` arm/capture, `Esc` close).
   - Hooks `LevelRenderer::renderLevel`, `LevelRendererPlayer::render`, and `LevelRendererPlayer::renderOutlineSelection` to drive world-space capture visuals and camera/state extraction.
   - Calls `PreviewRenderer::EnsureDX11/12`, `PreviewRenderer::FlushRenderDX11/12` (before ImGui draws onto the swapchain backbuffer), and `BlockTextureRegistry` + `UiAssets` lifecycle hooks.

2. **Minecraft Bedrock `1.26.6501.0` SDK & Reverse-Engineering Layer (`src/SDK/`, `src/Utils/Sig/SigInit.cpp`, `src/Utils/Offset/OffsetInit.cpp`, `src/Utils/SignatureAndOffsetManager.*`)**
   - **Verified Memory Signatures (`SigInit::init260`):**
     - Rendering: `LevelRenderer::renderLevel`, `LevelRendererPlayer::render`, `LevelRendererPlayer::renderOutlineSelection`, `MeshHelpers::renderMeshImmediately`, `Tessellator::begin`, `Tessellator::color`, `Tessellator::vertex`, `Tessellator::end`, `mce::Mesh::render`, `mce::Mesh::~Mesh`, `RenderMaterialGroup::getOrCreate`.
     - Input & Audio: `GameCore::pointerHandler`, `GameCore::rawMouseHandler`, `GameCore::mouseBtnHandler`, `GameCore::mouseMoveHandler`, `GameCore::grabMouse`, `GameCore::releaseMouse`, `MouseDevice::feed_126`, `SoundEngine::play_126`.
   - **Verified Class Offsets & VTable Indices (`OffsetInit::init260`):**
     - `ClientInstance`: `minecraftGame` (`0x1A0`), `levelRenderer` (`0x1B8`), `getPacketSender` (`0x1C8`), `camera` (`0x358`), `guiData` (`0x648`), `getLevelVIdx` (`167`), `getLocalPlayerVIdx` (`173`).
     - `LevelRenderer`: `clientInstance` (`0x1B8`).
     - `LevelRendererPlayer`: `cameraPos` (`0x658`), `selectionOverlayMat` (`0x1088`), `selectionBoxMat` (`0x10E8`), `clientInstance` (`0x1160`).
     - `ScreenContext` / `Tessellator`: `colorHolder` (`0x30`), `tessellator` (`0xB0`), `Tessellator::postTransformOffset` (`0x1CC`).
     - `Block` / `BlockLegacy` / `BlockSource` / `Level` / `Actor`:
       - `Block::blockLegacy` (`0x68`), `Block::serializationId` (`0xF0` — `CompoundTag` with block `name` + `states` for reading exact palette entries in memory!).
       - `BlockLegacy::name` (`0xA8`), `BlockLegacy::namespace` (`0xD0`), `BlockLegacy::mMapColor` (`0x1B8`), `BlockLegacy::mDefaultState` (`0x2E0`).
       - `BlockSource::getBlockVIdx` (`2`), `BlockSource::getChunkVIdx` (`40`).
       - `Level::getHitResultVIdx` (`335`), `HitResult::blockPos` (`0x20`), `Actor::getCarriedItemVIdx` (`77`).
     - UI Tree: `ScreenView::VisualTree` (`0x48`), `VisualTree::root` (`0x8`), `UIControl::LayerName` (`0x20`).
   - **SEH Crash Safety:** MSVC requires keeping C++ objects with unwindable destructors out of `__try`/`__except` frames; all raw pointer dereferences in `BedrockGame.cpp` use POD SEH helper functions.
   - **Camera & World-to-Screen (`BedrockGame.cpp`):** Reads `gfx_field_of_view` from the user's `options.txt` (`%APPDATA%\Minecraft Bedrock\Users\...\options.txt`) for `WorldToScreen` / `ProjectLine3D`, and drives native `Tessellator` + `renderMeshImmediately` using `LevelRendererPlayer::selectionBoxMat` (`0x10E8`) for world-locked selection outlines (`DrawDualOutlineBox3D` — avoids Vibrant Visuals overlay z-fighting).

3. **Core Schematic & Capture Engine (`src/Core/`)**
   - `SchematicDocument` (`SchematicDocument.hpp/.cpp`):
     - Stores dimensions (`width`, `height`, `depth`), `originX/Y/Z`, deduplicated `palette` (`BlockPaletteEntry` with `name`, `BlockStateProp[]`, `approxAbgr`, `isSolid`), sparse `16x16x16` `Subchunk16` maps (`SchematicPart`), and optional thumbnail RGBA.
     - `BuildMaterialTally()` aggregates block counts sorted descending.
   - `CaptureManager` (`CaptureManager.hpp/.cpp`):
     - Interactive bare-hand right-click + `[B]` capture workflow (`Idle` -> `ArmedNeedsPos1` -> `HasPos1NeedsPos2` -> `BoxLockedReadyToSave`).
     - Streams blocks via `BlockSource::getBlockVIdx` + `Block::serializationId` (`CompoundTag` name + states) across loaded chunks (`IsChunkColumnLoaded`), renders in-world `Pos 1` / `Pos 2` selection boxes and HUD corner markers, and exports `.bmet`.

4. **File Codecs (`src/IO/`)**
   - `BmetCodec` (`BmetCodec.hpp/.cpp`): Custom unlimited-size compressed binary format (`.bmet`) preserving full palette, block states, subchunks, and material summary.
   - `McStructureCodec` (`McStructureCodec.hpp/.cpp`): Vanilla Bedrock `.mcstructure` v1/v2 NBT reader & writer (`size`, `structure.block_indices`, `structure.palette.default.block_palette`, `structure_world_origin`), with `64x384x64` vanilla export warning/support.

5. **UI, Texture Atlas & 3D Preview (`src/UI/`)**
   - `SchematicMenu.cpp`, `UiAssets.cpp`, `UiDraw.cpp`: Custom dark-slate / gold-header Minecraft-styled ImGui overlay with:
     - Left panel: Schematic library (`+ IMPORT` in-UI modal — never native Windows file dialog, `FOLDER`, search filter, `.bmet` / `.mcstructure` badges).
     - Right panel: `3D VIEW` tab + `MATERIALS` tab (with stack math + textures), `DELETE`, `RENAME`, `EXPORT` modal (`.mcstructure` + `.bmet`), and `LOAD` button.
   - `BlockTextureRegistry` (`BlockTextureRegistry.hpp/.cpp`):
     - Reads directly from Minecraft's installed `data/resource_packs/vanilla*` folders (`blocks.json`, `textures/terrain_texture.json`, `blocks.brarchive`, `textures/blocks/*.brarchive`, loose `.png`/`.tga` decoded via WIC), packing `16x16` tiles into a `1024x1024` (`64x64` = `4096` slots) GPU atlas with Point/Nearest-Neighbor filtering.
   - `PreviewRenderer` (`PreviewRenderer.hpp/.cpp`):
     - Upgraded from CPU `ImDrawList` painter's sort to an offscreen **Hardware Z-Buffered `896x576` Viewport** (`R8G8B8A8_UNORM` + `D32_FLOAT` depth buffer) supporting both **D3D11** and **D3D12**.
     - Render-on-dirty (only redraws on mesh/camera/zoom change), baked per-vertex AO (`computeAo`) with anisotropic quad diagonal flip (`a0 + a2 >= a1 + a3`), cutout `clip(tex.a - 0.12f)`, and separate opaque + translucent (`water`/`glass`/`ice`) passes.

---

## 2. Design & Engineering Decisions / Constraints

- **Code Style:** Zero `//` or `/* */` comments and zero non-ASCII characters in `.cpp` / `.hpp` source files.
- **No Native File Dialogs:** Always use in-UI modals (`+ IMPORT` lists files in the schematics directory).
- **Safe Process Handling:** Never force-kill `Minecraft.Windows.exe` or edit `options.txt`; check if `Minecraft.Windows.exe` is running before linking `Bedrockmetica.dll` (`LNK1104`).
- **Build Environment:** Use Visual Studio's CMake directly (`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "...\build" --config Release`). Windows PowerShell 5.1 does not support `&&` (use `;` or `if ($LASTEXITCODE -eq 0)`).
- **D3D12 Interop Lessons Learned:**
  - `ImTextureID` uses `static_cast<ImTextureID>(gpuH.ptr)` (not `reinterpret_cast` on `UINT64`).
  - HLSL `float4x4` defaults to `column_major`, so C++ `ComputeMvpMatrix` must write `outM16[col * 4 + row]`.
  - After `PreviewRenderer::FlushRenderDX12`, `SwapchainHook` must rebind `g_d3d12RtvHandles[idx]` and `g_pd3d12SrvDescHeap` before `ImGui_ImplDX12_RenderDrawData`.
  - Cutout opaque pixels must write `alpha = 1.0f` to the offscreen RT after `clip(tex.a - 0.12f)` so ImGui compositing does not make leaves semi-transparent.
- **UI Terminology:** Do not refer to UI cards/styles as "Hive".

---

## 3. Completed Features (Committed & Pushed through `e088609`)

- **Phase 1 & 2 (Capture & I/O):**
  - Bare-hand Pos1 / Pos2 capture, chunk-safe block + `CompoundTag` state serialization, `.bmet` binary codec, `.mcstructure` v1/v2 NBT import & export, in-UI `+ IMPORT` & `EXPORT` modals, world-locked `DrawDualOutlineBox3D` selection box, world-exit reset, and FOV-accurate corner waypoints.
- **Phase 2.2 (Native Vanilla Textures):**
  - `blocks.brarchive` + `terrain_texture.json` + `blocks.json` extraction across all `vanilla_*` packs, WIC PNG/TGA decoding, foliage/water biome tinting, and material list display.
- **Phase 3 (Hardware Z-Buffered 3D Preview):**
  - D3D11/D3D12 offscreen `896x576` Z-buffered render target with orbit/pitch/zoom, vertex AO, opaque + translucent passes, hidden `light_block`/`barrier`/`structure_void`, neighbor-connected walls/fences/glass panes, stairs/slabs, and crops.

---

## 4. Known Issues & Immediate Next Work (Phase 3.5 Polish)

Before starting **Phase 4 (In-World Hologram)**, finish the remaining 3D preview & texture accuracy items:

1. **Finish & Compile Dynamic GPU Atlas Upload (`BlockTextureRegistry`):**
   - Already edited in `BlockTextureRegistry.hpp/.cpp` (`SyncGpuDX11` / `SyncGpuDX12` + `short_grass`/`tall_grass` tint fix); needs `SyncGpuDX11(ctx)` / `SyncGpuDX12(cmd)` wired into `PreviewRenderer::FlushRenderDX11` / `FlushRenderDX12` so newly discovered blocks (e.g., `leaf_litter` from `vanilla_1.21.70`) upload to GPU immediately without restart.
2. **Data-Driven `0..16` Cuboid + Per-Face Pixel Sub-UV Engine (`PreviewRenderer.cpp`):**
   - Add `emitCuboid(from[3], to[3], perFaceUV[4], rotDeg)` inspired by `tomocraft/mcstructure-preview`'s generic cuboid/sub-UV model pipeline, while keeping native Bedrock textures:
     - `grass_path` / `dirt_path` & `farmland`: `15/16` height (`to.y = 15`) with side sub-UV `[0, 1, 16, 16]` to eliminate the 1px top gap.
     - `lantern` / `soul_lantern`: exact multi-cuboid model (`[5,0,5]->[11,7,11]`, `[6,7,6]->[10,9,10]`, hanging chain) with packed sprite sub-UVs (`[0,2,6,9]`, `[0,9,6,15]`, `[1,0,5,2]`, `[11,1,14,8]`).
     - `lightning_rod` & `end_rod`: `facing_direction`-oriented shaft + tip cuboids with sprite sub-UVs instead of full 1x1x1 cubes.
     - `leaf_litter`, `wildflowers`, `pink_petals`, `carpet`: `1/16` flat ground cuboids (`[0,0,0]->[16,1,16]`).
     - `chain`, `torch`, `trapdoor`, `door`, `fence_gate`, `campfire`, `candle`: compact `0..16` cuboids with proper sub-UVs.
3. **Banner Block-Entity Patterns (`standing_banner` / `wall_banner`):**
   - Parse `block_entity_data` NBT (`Base` color + `Patterns[]`) in `McStructureCodec` / `BmetCodec` / `CaptureManager` and composite `textures/entity/banner/*.png`.

---

## 5. Remaining Roadmap

- **Phase 4 — In-World Hologram & Ghost Placement (`LOAD` button):**
  - Render the selected schematic as a world-anchored 3D ghost hologram inside Minecraft (`LevelRenderer` / `Tessellator` or D3D11/D3D12 depth-tested world pass + shader/wireframe/layer slice modes: `ALL`, `SINGLE`, `UP TO`).
  - Placement controls: world `X/Y/Z` offset nudging, `90°` rotation, `X/Z` mirror, lock/unlock position.
  - Real-time schematic verification overlay (comparing placed blocks in `BlockSource` against schematic palette: missing, wrong block, wrong state, extra) + live `MATERIALS` missing-items HUD.
- **Phase 5 — Polish & QoL:**
  - Easy-place / verifier highlights, configurable render distance/chunk culling for very large builds (`47x61x78` / `212k+` blocks), and `SETTINGS` persistence.
