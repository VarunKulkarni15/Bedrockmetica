#include "SigInit.hpp"
#include "../SignatureAndOffsetManager.hpp"
#include "../Logger.hpp"

void SigInit::init260() {
    Logger::info("Loading verified memory signatures for Minecraft Bedrock 1.26.6501.0", "Signatures");

    ADD_SIG("LevelRenderer::renderLevel", "55 41 57 41 56 41 54 56 57 53 48 81 EC C0 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 38 FE FF FF FF 4C 89 C3 48 89 D6 48 89 CF 8B 05 ? ? ? ?");
    ADD_SIG("LevelRendererPlayer::render", "41 57 41 56 56 57 55 53 48 83 EC 48 4D 89 CE 4C 89 C6 48 89 D7 48 89 CB 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 40");
    ADD_SIG("LevelRendererPlayer::renderOutlineSelection", "55 41 57 41 56 41 54 56 57 53 48 81 EC 20 02 00 00 48 8D AC 24 80 00 00 00 44 0F 29 85 90 01 00");
    ADD_SIG("MeshHelpers::renderMeshImmediately", "55 41 57 41 56 41 54 56 57 53 48 81 EC 50 04 00 00 48 8D AC 24 80 00 00 00 48 C7 85 C8 03 00 00 FE FF FF FF 80 BA 55 02 00 00 00");
    ADD_SIG("Tessellator::begin", "56 57 55 53 48 83 EC 38 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 30 80 B9 A0 02 00 00 00 0F 85 ? ? ? ? 48 89 CE");
    ADD_SIG("Tessellator::color", "80 B9 54 02 00 00 00 0F 85 ? ? ? ? F3 0F 10 05 ? ? ? ? F3 0F 59 C8 F3 0F 2C C1 45 31 C0");
    ADD_SIG("Tessellator::vertex", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 78 44 0F 29 4C 24 60 44 0F 29 44 24 50 0F 29 7C 24 40 0F 29 74 24 30 8B 81 98 02 00 00");
    ADD_SIG("Tessellator::end", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 88 03 00 00 48 8D AC 24 80 00 00 00 44 0F 29 AD F0 02 00 00");
    ADD_SIG("mce::Mesh::render", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 38 04 00 00 48 8D AC 24 80 00 00 00 0F 29 B5 A0 03 00 00 48 C7 85 98 03 00 00 FE FF FF FF 4D 89 CC 4D 89 C6 48");
    ADD_SIG("mce::Mesh::~Mesh", "56 57 48 83 EC 28 48 89 CE E8 ? ? ? ? 48 8B 8E 48 02 00 00 48 85 C9 74 49 48 8B 96 58 02 00 00");
    ADD_SIG("RenderMaterialGroup::getOrCreate", "55 56 57 48 83 EC 50 48 8D 6C 24 50 0F 29 75 F0 48 C7 45 E8 FE FF FF FF 49 89 D1 0F 57 F6 48 89 CF 0F 11 31 48 8B 02 48 8B 40 08 48 8D 55 D0 4C 89 C9 FF");

    ADD_SIG("GameCore::pointerHandler", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 ec 68 4d 89 c7 48 89 d6 48 89");
    ADD_SIG("GameCore::rawMouseHandler", "41 56 56 57 55 53 48 83 ec 50 48 8b 05 ? ? ? ? 48 31 e0 48 89 44 24 ? 49 8b 00 48 83 78 68");
    ADD_SIG("GameCore::mouseBtnHandler", "41 56 56 57 55 53 48 83 EC 60 4C 89 C7 48 89 D6 48 8B 05 ? ? ? ? 48 31 E0 48 89");
    ADD_SIG("GameCore::mouseMoveHandler", "41 57 41 56 41 55 41 54 56 57 53 48 83 EC 60 4C 89 C7 48 89 D6 49 8B 18 80 BB F8 00");
    ADD_SIG("GameCore::grabMouse", "56 57 53 48 83 EC 60 48 89 CE 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 58 80 B9 A9");
    ADD_SIG("GameCore::releaseMouse", "56 57 48 83 EC 28 48 89 CE B9 01 00 00 00 FF 15 ? ? ? ? 85 C0 79 13 48 8B 3D");
    ADD_SIG("MouseDevice::feed_126", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 44 89 CF 44 89 C3 89 D5 48 89 CE 44");
    ADD_SIG("SoundEngine::play_126", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 28 03 00 00 48 8D AC 24 80 00 00 00 0F 29 BD 90 02 00 00 0F 29 B5 80 02 00 00 48 C7 85 78 02 00 00 FE FF FF FF 0F 28 F3 F3 0F 10 0D");

    ADD_SIG("BlockTypeRegistry::lookupByName", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 98 01 00 00 48 8D AC 24 80 00 00 00 0F 29 B5 00 01 00 00 48 C7 85 F8 00 00 00 FE FF FF FF 49 8B 70 18 48 85 F6 74 4A 4C 89 C7");
    ADD_SIG("BlockTypeRegistry::mMap_ref", "48 8D 0D ? ? ? ? 4C 8D 05 ? ? ? ? 48 8D 54 24 30 45 31 C9 E8 ? ? ? ? 48 8B 44 24 30 48 85 C0 75 44 48 8D 3D ? ? ? ?");
    ADD_SIG("BlockTessellator::ctor", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 38 01 00 00 48 8D AC 24 80 00 00 00 0F 29 B5 A0 00 00 00 48 C7 85 98 00 00 00 FE FF FF FF 48 89 D6 49 89 CF 66 C7 01 00 00");
    ADD_SIG("BlockTessellator::prepare", "41 56 56 57 53 48 83 EC 48 48 89 CE 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 40 48 8B 41 08 48 89 81 C8 06 00 00 8B 42 08 F3 0F 7E 02");
    ADD_SIG("BlockTessellator::setupCache", "41 56 56 57 53 48 83 EC 48 48 89 CE 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 40 48 8B 41 08 48 89 81 C8 06 00 00 8B 42 08 83 C0 FE");
    ADD_SIG("BlockTessellator::tessellateInWorld", "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC B8 00 00 00 0F 29 B4 24 A0 00 00 00 4C 89 CF 4D 89 C6");
    ADD_SIG("BlockTessellator::tessellateExtra", "41 57 41 56 41 55 41 54 56 57 53 48 83 EC 60 0F 29 74 24 50 4C 89 CF 4D 89 C6");
    ADD_SIG("Tessellator::ctor", "55 41 57 41 56 56 57 53 48 81 EC F8 00 00 00 48 8D AC 24 80 00 00 00 0F 29 75 60 48 C7 45 58 FE FF FF FF 48 89 D3 49 89 CE 48 8D 35 ? ? ? ?");
    ADD_SIG("TextureGroup::getTexture", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 58 01 00 00 48 8D AC 24 80 00 00 00 48 C7 85 D0 00 00 00 FE FF FF FF 44 88 8D CF 00 00 00");
    ADD_SIG("bgfx_s_ctx_ref", "4C 8B 0D ? ? ? ? 41 80 79 55 01 75 3B 44 88 D0 C1 E0 03");
}
