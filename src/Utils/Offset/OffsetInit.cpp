#include "OffsetInit.hpp"
#include "../SignatureAndOffsetManager.hpp"
#include "../Logger.hpp"

void OffsetInit::init260() {
    Logger::info("Loading verified offsets for Minecraft Bedrock 1.26.6501.0", "Offsets");

    ADD_OFFSET("ClientInstance::minecraftGame", 0x1A0);
    ADD_OFFSET("ClientInstance::levelRenderer", 0x1B8);
    ADD_OFFSET("ClientInstance::getPacketSender", 0x1C8);
    ADD_OFFSET("ClientInstance::camera", 0x358);
    ADD_OFFSET("ClientInstance::guiData", 0x648);
    ADD_OFFSET("ClientInstance::getLevelVIdx", 167);
    ADD_OFFSET("ClientInstance::getLocalPlayerVIdx", 173);

    ADD_OFFSET("LevelRenderer::clientInstance", 0x1B8);
    ADD_OFFSET("LevelRendererPlayer::cameraPos", 0x658);
    ADD_OFFSET("LevelRendererPlayer::selectionOverlayMat", 0x1088);
    ADD_OFFSET("LevelRendererPlayer::selectionBoxMat", 0x10E8);
    ADD_OFFSET("LevelRendererPlayer::clientInstance", 0x1160);

    ADD_OFFSET("ScreenContext::colorHolder", 0x30);
    ADD_OFFSET("ScreenContext::tessellator", 0xB0);
    ADD_OFFSET("Tessellator::postTransformOffset", 0x1CC);

    ADD_OFFSET("Block::blockLegacy", 0x68);
    ADD_OFFSET("Block::serializationId", 0xF0);
    ADD_OFFSET("BlockSource::getBlockVIdx", 2);
    ADD_OFFSET("BlockSource::getChunkVIdx", 40);
    ADD_OFFSET("Level::getHitResultVIdx", 335);
    ADD_OFFSET("HitResult::blockPos", 0x20);
    ADD_OFFSET("Actor::getCarriedItemVIdx", 77);

    ADD_OFFSET("BlockLegacy::name", 0xA8);
    ADD_OFFSET("BlockLegacy::namespace", 0xD0);
    ADD_OFFSET("BlockLegacy::mMapColor", 0x1B8);
    ADD_OFFSET("BlockLegacy::mDefaultState", 0x2E0);
    ADD_OFFSET("ScreenView::VisualTree", 0x48);
    ADD_OFFSET("VisualTree::root", 0x8);
    ADD_OFFSET("UIControl::LayerName", 0x20);
}
