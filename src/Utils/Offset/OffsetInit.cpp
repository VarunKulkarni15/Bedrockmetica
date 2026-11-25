#include "OffsetInit.hpp"
#include "../SignatureAndOffsetManager.hpp"
#include "../Logger.hpp"

void OffsetInit::init260() {
    Logger::info("Loading verified offsets for Minecraft Bedrock 1.26.X", "Offsets");

    ADD_OFFSET("ClientInstance::minecraftGame", 0x1A0);
    ADD_OFFSET("ClientInstance::levelRenderer", 0x1B8);
    ADD_OFFSET("ClientInstance::getPacketSender", 0x1C8);
    ADD_OFFSET("ClientInstance::camera", 0x358);
    ADD_OFFSET("ClientInstance::guiData", 0x648);
    ADD_OFFSET("MinecraftGame::gameRenderer", 0xD70);
    ADD_OFFSET("LevelRender::levelRendererPlayer", 0x430);
    ADD_OFFSET("LevelRendererPlayer::cameraPos", 0x704);
    ADD_OFFSET("Block::blockLegacy", 0x60);
    ADD_OFFSET("BlockLegacy::name", 0xA8);
    ADD_OFFSET("BlockLegacy::namespace", 0xD0);
    ADD_OFFSET("BlockLegacy::mMapColor", 0x1B8);
    ADD_OFFSET("BlockLegacy::mDefaultState", 0x2E0);
    ADD_OFFSET("ScreenView::VisualTree", 0x48);
    ADD_OFFSET("VisualTree::root", 0x8);
    ADD_OFFSET("UIControl::LayerName", 0x20);
}
