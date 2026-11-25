#include "SoundManager.hpp"
#include "CrashGuard.hpp"
#include "RenderUtils.hpp"
#include "SignatureAndOffsetManager.hpp"
#include "Logger.hpp"
#include <minhook/include/MinHook.h>

#include <atomic>
#include <mutex>
#include <cmath>
#include <vector>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace SoundManager {
    typedef uint64_t(__fastcall* SoundEnginePlayFn)(void* SoundEngine, const std::string& name, RenderUtils::Vector3 pos, float volume, float pitch);
    typedef void*(__fastcall* SoundEnginePlay7Fn)(void* SoundEngine, const std::string& name, const RenderUtils::Vector3& pos, float volume, float pitch, bool forceUnmuted, void* outHandle);

    static std::atomic<void*> g_pSoundEngine{ nullptr };
    static SoundEnginePlayFn g_pfnSoundEnginePlay = nullptr;
    static SoundEnginePlayFn g_oSoundEnginePlay = nullptr;
    static SoundEnginePlay7Fn g_pfnSoundEnginePlay7 = nullptr;
    static SoundEnginePlay7Fn g_oSoundEnginePlay7 = nullptr;
    static std::atomic<bool> g_bHooked{ false };
    static std::atomic<bool> g_bSoundsEnabled{ true };

    static std::vector<uint8_t> BuildPcmWav(int sampleRate, const std::vector<int16_t>& samples) {
        uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
        uint32_t riffBytes = 36 + dataBytes;
        std::vector<uint8_t> wav(44 + dataBytes);

        wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
        *reinterpret_cast<uint32_t*>(&wav[4]) = riffBytes;
        wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';

        wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
        *reinterpret_cast<uint32_t*>(&wav[16]) = 16;
        *reinterpret_cast<uint16_t*>(&wav[20]) = 1;
        *reinterpret_cast<uint16_t*>(&wav[22]) = 1;
        *reinterpret_cast<uint32_t*>(&wav[24]) = static_cast<uint32_t>(sampleRate);
        *reinterpret_cast<uint32_t*>(&wav[28]) = static_cast<uint32_t>(sampleRate * sizeof(int16_t));
        *reinterpret_cast<uint16_t*>(&wav[32]) = static_cast<uint16_t>(sizeof(int16_t));
        *reinterpret_cast<uint16_t*>(&wav[34]) = 16;

        wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
        *reinterpret_cast<uint32_t*>(&wav[40]) = dataBytes;
        if (dataBytes > 0) {
            memcpy(&wav[44], samples.data(), dataBytes);
        }
        return wav;
    }

    static void PlaySynthesizedSound(UISound sound) {
        static const int SR = 22050;
        std::vector<int16_t> samples;

        switch (sound) {
        case UISound::Click: {
            int n = static_cast<int>(SR * 0.022);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float env = std::pow(1.0f - t, 2.8f);
                float wave = std::sin(6.2831853f * 1250.0f * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 24000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::Toggle: {
            int n = static_cast<int>(SR * 0.045);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float freq = 680.0f + 550.0f * (1.0f - t);
                float env = std::sin(t * 3.14159265f);
                float wave = std::sin(6.2831853f * freq * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 22000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::Success: {
            int n = static_cast<int>(SR * 0.16);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float env = (t < 0.15f) ? (t / 0.15f) : std::pow(1.0f - (t - 0.15f) / 0.85f, 2.0f);
                float f1 = (t < 0.45f) ? 880.0f : 1320.0f;
                float wave = std::sin(6.2831853f * f1 * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 26000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::Error: {
            int n = static_cast<int>(SR * 0.09);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float env = std::pow(1.0f - t, 1.8f);
                float wave = std::sin(6.2831853f * 180.0f * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 24000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::TabNav:
        case UISound::FloorStep: {
            int n = static_cast<int>(SR * 0.018);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float env = std::pow(1.0f - t, 3.2f);
                float wave = std::sin(6.2831853f * 1650.0f * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 20000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::OpenMenu: {
            int n = static_cast<int>(SR * 0.08);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float freq = 420.0f + 480.0f * t;
                float env = (t < 0.2f) ? (t / 0.2f) : (1.0f - t);
                float wave = std::sin(6.2831853f * freq * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 18000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        case UISound::CloseMenu: {
            int n = static_cast<int>(SR * 0.08);
            samples.resize(n);
            for (int i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / n;
                float freq = 820.0f - 420.0f * t;
                float env = (t < 0.2f) ? (t / 0.2f) : (1.0f - t);
                float wave = std::sin(6.2831853f * freq * (i / static_cast<float>(SR)));
                samples[i] = static_cast<int16_t>(std::clamp(wave * env * 18000.0f, -32767.0f, 32767.0f));
            }
            break;
        }
        default:
            return;
        }

        std::vector<uint8_t> wav = BuildPcmWav(SR, samples);
        PlaySoundA(reinterpret_cast<LPCSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    void SetEnabled(bool enabled) {
        g_bSoundsEnabled.store(enabled);
    }

    bool IsEnabled() {
        return g_bSoundsEnabled.load();
    }

    static uint64_t __fastcall hkSoundEnginePlay(void* SoundEngine, const std::string& name, RenderUtils::Vector3 pos, float volume, float pitch) {
        if (SoundEngine) {
            g_pSoundEngine.store(SoundEngine);
        }
        if (g_oSoundEnginePlay) {
            return g_oSoundEnginePlay(SoundEngine, name, pos, volume, pitch);
        }
        return 0;
    }

    static void* __fastcall hkSoundEnginePlay7(void* SoundEngine, const std::string& name, const RenderUtils::Vector3& pos, float volume, float pitch, bool forceUnmuted, void* outHandle) {
        if (SoundEngine) {
            g_pSoundEngine.store(SoundEngine);
        }
        if (g_oSoundEnginePlay7) {
            return g_oSoundEnginePlay7(SoundEngine, name, pos, volume, pitch, forceUnmuted, outHandle);
        }
        return outHandle;
    }

    void Initialize() {
        CrashGuard::TryExecute([&]() {
            uintptr_t play7Addr = Mgr.getSigAddress(Utils::hash("SoundEngine::play_126"));
            if (play7Addr) {
                g_pfnSoundEnginePlay7 = reinterpret_cast<SoundEnginePlay7Fn>(play7Addr);
                MH_STATUS status = MH_CreateHook(
                    reinterpret_cast<void*>(play7Addr),
                    reinterpret_cast<void*>(hkSoundEnginePlay7),
                    reinterpret_cast<void**>(&g_oSoundEnginePlay7)
                );
                if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
                    MH_EnableHook(reinterpret_cast<void*>(play7Addr));
                    g_bHooked.store(true);
                    Logger::success("SoundEngine::play_126 hooked cleanly for native Minecraft SFX.", "SoundManager");
                    return;
                }
            }

            uintptr_t playAddr = Mgr.getSigAddress(Utils::hash("SoundEngine::play"));
            if (!playAddr) {
                playAddr = Mgr.getSigAddress(Utils::hash("SoundEngine::play_alt"));
            }

            if (playAddr) {
                g_pfnSoundEnginePlay = reinterpret_cast<SoundEnginePlayFn>(playAddr);
                MH_STATUS status = MH_CreateHook(
                    reinterpret_cast<void*>(playAddr),
                    reinterpret_cast<void*>(hkSoundEnginePlay),
                    reinterpret_cast<void**>(&g_oSoundEnginePlay)
                );
                if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
                    MH_EnableHook(reinterpret_cast<void*>(playAddr));
                    g_bHooked.store(true);
                    Logger::success("SoundEngine::play hooked cleanly for authentic Minecraft Ore UI audio.", "SoundManager");
                }
            } else {
                Logger::warn("SoundEngine::play signature not resolved; using built-in synthetic audio fallback.", "SoundManager");
            }
        }, "SoundManager::Initialize");
    }

    void SetSoundEngine(void* pSoundEngine) {
        g_pSoundEngine.store(pSoundEngine);
    }

    void Play(UISound sound) {
        if (!g_bSoundsEnabled.load()) return;
        std::string soundName;
        float pitch = 1.0f;
        float volume = 0.60f;

        switch (sound) {
        case UISound::Click:
            soundName = "random.click";
            volume = 0.58f;
            pitch = 1.04f;
            break;
        case UISound::TabNav:
            soundName = "note.bit";
            volume = 0.55f;
            pitch = 1.15f;
            break;
        case UISound::Toggle:
            soundName = "random.pop";
            volume = 0.60f;
            pitch = 1.18f;
            break;
        case UISound::FloorStep:
            soundName = "note.hat";
            volume = 0.55f;
            pitch = 1.25f;
            break;
        case UISound::Success:
            soundName = "random.orb";
            volume = 0.65f;
            pitch = 1.10f;
            break;
        case UISound::Error:
            soundName = "note.bass";
            volume = 0.65f;
            pitch = 0.72f;
            break;
        case UISound::OpenMenu:
            soundName = "random.chestopen";
            volume = 0.36f;
            pitch = 1.38f;
            break;
        case UISound::CloseMenu:
            soundName = "random.chestclosed";
            volume = 0.36f;
            pitch = 1.35f;
            break;
        }

        void* pEngine = g_pSoundEngine.load();
        if (!pEngine) {
            PlaySynthesizedSound(sound);
            return;
        }

        if (g_pfnSoundEnginePlay7) {
            auto fn7 = g_oSoundEnginePlay7 ? g_oSoundEnginePlay7 : g_pfnSoundEnginePlay7;
            CrashGuard::TryExecute([&]() {
                RenderUtils::Vector3 listenerPos{ 0.0f, 0.0f, 0.0f };
                __try {
                    const auto* lp = reinterpret_cast<const RenderUtils::Vector3*>(reinterpret_cast<uintptr_t>(pEngine) + 0x11b0);
                    listenerPos = *lp;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
                uint8_t dummyHandle[32] = {};
                fn7(pEngine, soundName, listenerPos, volume, pitch, false, dummyHandle);
            }, "SoundManager::Play7");
            return;
        }

        if (g_pfnSoundEnginePlay) {
            auto fn = g_oSoundEnginePlay ? g_oSoundEnginePlay : g_pfnSoundEnginePlay;
            CrashGuard::TryExecute([&]() {
                auto camPos = RenderUtils::RenderContext::Get().GetActiveCameraPosition();
                fn(pEngine, soundName, camPos, volume, pitch);
            }, "SoundManager::Play");
            return;
        }

        PlaySynthesizedSound(sound);
    }
}
