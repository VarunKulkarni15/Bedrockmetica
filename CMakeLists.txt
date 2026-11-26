cmake_minimum_required(VERSION 3.24)
project(Bedrockmetica VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC Runtime (/MT) to avoid missing runtime DLL dependencies
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC Compilation Flags & SEH support (/EHa or SEH enabled)
add_compile_options(
    /utf-8
    /bigobj
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/Gy>
    $<$<CONFIG:Release>:/Gw>
    $<$<CONFIG:Release>:/GF>
)

add_definitions(
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
    -DUNICODE
    -D_UNICODE
    -DKIERO_INCLUDE_D3D11=1
    -DKIERO_INCLUDE_D3D12=1
    -DKIERO_USE_MINHOOK=1
)

# Project Include Directories
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Hooks"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/minhook/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/kiero"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/glm"
)

# Bedrockmetica Phase 1 Sources
set(BEDROCKMETICA_SOURCES
    src/dllmain.cpp
    src/Hooks/SwapchainHook.cpp
    src/Core/SchematicDocument.cpp
    src/Core/CaptureManager.cpp
    src/IO/BmetCodec.cpp
    src/IO/McStructureCodec.cpp
    src/SDK/BedrockGame.cpp
    src/UI/UiAssets.cpp
    src/UI/UiDraw.cpp
    src/UI/SchematicMenu.cpp

    # Utilities & Crash Protection
    src/Utils/CrashGuard.cpp
    src/Utils/ToastManager.cpp
    src/Utils/SoundManager.cpp
    src/Utils/RenderUtils.cpp
    src/Utils/SignatureAndOffsetManager.cpp
    src/Utils/Offset/OffsetInit.cpp
    src/Utils/Sig/SigInit.cpp

    # Kiero Universal Graphics Hook
    ext/kiero/kiero.cpp

    # MinHook Detour Engine
    ext/minhook/src/buffer.c
    ext/minhook/src/hook.c
    ext/minhook/src/trampoline.c
    ext/minhook/src/hde/hde64.c

    # Dear ImGui Core
    ext/imgui/imgui.cpp
    ext/imgui/imgui_draw.cpp
    ext/imgui/imgui_tables.cpp
    ext/imgui/imgui_widgets.cpp

    # Dear ImGui Backends
    ext/imgui/backends/imgui_impl_win32.cpp
    ext/imgui/backends/imgui_impl_dx11.cpp
    ext/imgui/backends/imgui_impl_dx12.cpp
)

add_library(Bedrockmetica SHARED ${BEDROCKMETICA_SOURCES})

# Windows System Libraries
target_link_libraries(Bedrockmetica PRIVATE
    d3d11
    d3d12
    dxgi
    d3dcompiler
    dwmapi
    version
    windowscodecs
    ole32
)

# Output Name Configuration
set_target_properties(Bedrockmetica PROPERTIES
    OUTPUT_NAME "Bedrockmetica"
    PREFIX ""
    SUFFIX ".dll"
)
