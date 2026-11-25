#include "SigInit.hpp"
#include "../SignatureAndOffsetManager.hpp"
#include "../Logger.hpp"

void SigInit::init260() {
    Logger::info("Loading verified memory signatures for Minecraft Bedrock 1.26.X", "Signatures");

    ADD_SIG("LevelRenderer::renderLevel", "48 8b c4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8d a8 ? ? ? ? 48 81 ec ? ? ? ? 0f 29 70 ? 0f 29 78 ? 44 0f 29 40 ? 44 0f 29 48 ? 44 0f 29 90 ? ? ? ? 44 0f 29 98 ? ? ? ? 44 0f 29 a0 ? ? ? ? 48 8b 05 ? ? ? ? 48 33 c4 48 89 85 ? ? ? ? 4d 8b f0");
    ADD_SIG("LocalPlayer::applyTurnDelta", "48 8b c4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8d 68 ? 48 81 ec ? ? ? ? 0f 29 70 ? 0f 29 78 ? 44 0f 29 40 ? 44 0f 29 48 ? 44 0f 29 50 ? 44 0f 29 98 ? ? ? ? 48 8b 05 ? ? ? ? 48 33 c4 48 89 45 ? 4c 8b ea");
    ADD_SIG("ClientInstance::_updateScreenSizeVariables", "48 8B C4 48 89 58 ? 48 89 70 ? 55 57 41 54 41 56 41 57 48 8D 68 ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ? 44 0F 29 98 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 0F 28 FB");
    ADD_SIG("Actor::vtable", "48 8D 05 ? ? ? ? 48 89 01 49 8B 01 48 89 41 08 49 8B 41 08 48 89 41 10 41 8B 41 10 89 41 18 48 89 69");
    ADD_SIG("mce::TextureGroup::getTexture", "48 89 54 24 ? 53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec ? ? ? ? 48 8b 05 ? ? ? ? 48 33 c4 48 89 84 24 ? ? ? ? 44 88 4c 24");
    ADD_SIG("SoundEngine::play", "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC E8 00 00 00 0F 29 B4 24 D0 00 00 00");
    ADD_SIG("SoundEngine::play_alt", "40 53 55 56 57 41 56 41 57 48 81 EC C8 00 00 00 0F 29 B4 24 B0 00 00 00");
    ADD_SIG("ScreenView::setupAndRender", "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 BC 24 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA");
    ADD_SIG("ScreenView::setupAndRender_alt", "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA 48 89 55");
    ADD_SIG("ScreenView::setupAndRender_alt2", "48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FD");
    ADD_SIG("InputHandler::tick", "E8 ?? ?? ?? ?? 48 8B 5E 78 33 ED");
    ADD_SIG("InputHandler::tick_alt", "E8 ?? ?? ?? ?? 48 8B 5F 70 45 33 FF");
    ADD_SIG("MouseInputVector", "48 2B 05 ? ? ? ? 8B 0D");
    ADD_SIG("MouseDevice::feed", "E8 ? ? ? ? 40 88 6C 1F");
    ADD_SIG("GameCore_handleMouseInput", "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4C 89 45 ? 32 C0");
    ADD_SIG("GameCore::pointerHandler", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 ec 68 4d 89 c7 48 89 d6 48 89");
    ADD_SIG("GameCore::rawMouseHandler", "41 56 56 57 55 53 48 83 ec 50 48 8b 05 ? ? ? ? 48 31 e0 48 89 44 24 ? 49 8b 00 48 83 78 68");
    ADD_SIG("GameCore::mouseBtnHandler", "41 56 56 57 55 53 48 83 EC 60 4C 89 C7 48 89 D6 48 8B 05 ? ? ? ? 48 31 E0 48 89");
    ADD_SIG("GameCore::mouseMoveHandler", "41 57 41 56 41 55 41 54 56 57 53 48 83 EC 60 4C 89 C7 48 89 D6 49 8B 18 80 BB F8 00");
    ADD_SIG("GameCore::grabMouse", "56 57 53 48 83 EC 60 48 89 CE 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 58 80 B9 A9");
    ADD_SIG("GameCore::releaseMouse", "56 57 48 83 EC 28 48 89 CE B9 01 00 00 00 FF 15 ? ? ? ? 85 C0 79 13 48 8B 3D");
    ADD_SIG("MouseDevice::feed_126", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 44 89 CF 44 89 C3 89 D5 48 89 CE 44");
    ADD_SIG("SoundEngine::play_126", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 28 03 00 00 48 8D AC 24 80 00 00 00 0F 29 BD 90 02 00 00 0F 29 B5 80 02 00 00 48 C7 85 78 02 00 00 FE FF FF FF 0F 28 F3 F3 0F 10 0D");
}
