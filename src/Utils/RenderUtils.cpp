#include "RenderUtils.hpp"
#include <algorithm>
#include <numbers>

namespace RenderUtils {

    RenderContext::RenderContext() {
        // Initialize identity
        for (int i = 0; i < 16; ++i) {
            m_gameVPMatrix.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            m_syntheticVPMatrix.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }

    void RenderContext::UpdateGameMatrix(const float* vpMatrix16, const Vector3& camPos) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (vpMatrix16) {
            std::copy(vpMatrix16, vpMatrix16 + 16, m_gameVPMatrix.m);
            m_hasGameMatrix = true;
        }
        m_gameCamPos = camPos;
    }

    void RenderContext::SetCameraOrigin(const Vector3& camOrigin) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_camOrigin = camOrigin;
    }

    Vector3 RenderContext::GetCameraOrigin() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_camOrigin;
    }

    void RenderContext::GetActiveViewProjection(float* outMatrix16) const {
        if (!outMatrix16) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_useSyntheticCamera || !m_hasGameMatrix) {
            std::copy(m_syntheticVPMatrix.m, m_syntheticVPMatrix.m + 16, outMatrix16);
        } else {
            std::copy(m_gameVPMatrix.m, m_gameVPMatrix.m + 16, outMatrix16);
        }
    }

    Vector3 RenderContext::GetActiveCameraPosition() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_useSyntheticCamera || !m_hasGameMatrix) {
            return m_syntheticCamPos;
        }
        return m_gameCamPos;
    }

    void RenderContext::BuildSyntheticMatrix(float screenWidth, float screenHeight) {
        if (screenWidth <= 0.0f || screenHeight <= 0.0f) return;

        float aspect = screenWidth / screenHeight;
        float fovY = 70.0f * (3.1415926535f / 180.0f);
        float nearZ = 0.1f;
        float farZ = 1000.0f;

        // Perspective Projection Matrix
        float tanHalfFov = std::tan(fovY * 0.5f);
        float proj[16] = { 0 };
        proj[0] = 1.0f / (aspect * tanHalfFov);
        proj[5] = 1.0f / tanHalfFov;
        proj[10] = farZ / (farZ - nearZ);
        proj[11] = 1.0f;
        proj[14] = -(farZ * nearZ) / (farZ - nearZ);
        proj[15] = 0.0f;

        // View Matrix from yaw/pitch/pos
        float yawRad = m_syntheticYaw * (3.1415926535f / 180.0f);
        float pitchRad = m_syntheticPitch * (3.1415926535f / 180.0f);

        Vector3 forward{
            std::cos(pitchRad) * std::sin(yawRad),
            std::sin(pitchRad),
            std::cos(pitchRad) * std::cos(yawRad)
        };
        Vector3 right{ std::cos(yawRad), 0.0f, -std::sin(yawRad) };
        Vector3 up{
            right.y * forward.z - right.z * forward.y,
            right.z * forward.x - right.x * forward.z,
            right.x * forward.y - right.y * forward.x
        };

        float view[16] = {
            right.x, up.x, forward.x, 0,
            right.y, up.y, forward.y, 0,
            right.z, up.z, forward.z, 0,
            -(right.x * m_syntheticCamPos.x + right.y * m_syntheticCamPos.y + right.z * m_syntheticCamPos.z),
            -(up.x * m_syntheticCamPos.x + up.y * m_syntheticCamPos.y + up.z * m_syntheticCamPos.z),
            -(forward.x * m_syntheticCamPos.x + forward.y * m_syntheticCamPos.y + forward.z * m_syntheticCamPos.z),
            1
        };

        // Multiply View * Proj -> VP
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += view[row * 4 + k] * proj[k * 4 + col];
                }
                m_syntheticVPMatrix.m[row * 4 + col] = sum;
            }
        }
    }

    bool WorldToScreen(const Vector3& worldPos, const float* vpMatrix, float screenWidth, float screenHeight, Vector2& outScreen) {
        if (!vpMatrix || screenWidth <= 0.0f || screenHeight <= 0.0f) return false;

        Vector3 camOrigin = RenderContext::Get().GetCameraOrigin();
        Vector3 relPos = worldPos;
        if (RenderContext::Get().HasValidGameMatrix()) {
            relPos = worldPos - camOrigin;
        }

        // Column-major or row-major transform
        // Minecraft Bedrock matrix in DirectX layout:
        float clipX = relPos.x * vpMatrix[0] + relPos.y * vpMatrix[4] + relPos.z * vpMatrix[8]  + vpMatrix[12];
        float clipY = relPos.x * vpMatrix[1] + relPos.y * vpMatrix[5] + relPos.z * vpMatrix[9]  + vpMatrix[13];
        float clipZ = relPos.x * vpMatrix[2] + relPos.y * vpMatrix[6] + relPos.z * vpMatrix[10] + vpMatrix[14];
        float clipW = relPos.x * vpMatrix[3] + relPos.y * vpMatrix[7] + relPos.z * vpMatrix[11] + vpMatrix[15];

        // Near-plane clipping: clip behind camera
        if (clipW < 0.05f) {
            return false;
        }

        // Perspective divide -> NDC [-1, 1]
        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;

        if (ndcX < -1.5f || ndcX > 1.5f || ndcY < -1.5f || ndcY > 1.5f) return false;

        // Viewport mapping -> Screen Coordinates
        outScreen.x = (ndcX + 1.0f) * 0.5f * screenWidth;
        outScreen.y = (1.0f - ndcY) * 0.5f * screenHeight;

        return true;
    }

    void Draw3DWireframeBox(
        ImDrawList* drawList,
        const Vector3& minPos,
        const Vector3& maxPos,
        const float* vpMatrix,
        float screenWidth,
        float screenHeight,
        ImU32 outlineColor,
        ImU32 faceColor,
        float lineThickness
    ) {
        if (!drawList || !vpMatrix) return;

        // 8 Corners of the AABB
        Vector3 v[8] = {
            { minPos.x, minPos.y, minPos.z }, // 0
            { maxPos.x, minPos.y, minPos.z }, // 1
            { maxPos.x, maxPos.y, minPos.z }, // 2
            { minPos.x, maxPos.y, minPos.z }, // 3
            { minPos.x, minPos.y, maxPos.z }, // 4
            { maxPos.x, minPos.y, maxPos.z }, // 5
            { maxPos.x, maxPos.y, maxPos.z }, // 6
            { minPos.x, maxPos.y, maxPos.z }  // 7
        };

        Vector2 s[8];
        bool valid[8];

        for (int i = 0; i < 8; ++i) {
            valid[i] = WorldToScreen(v[i], vpMatrix, screenWidth, screenHeight, s[i]);
        }

        // 6 Faces (Quads)
        static const int faces[6][4] = {
            { 0, 1, 2, 3 }, // Front (-Z)
            { 5, 4, 7, 6 }, // Back (+Z)
            { 4, 0, 3, 7 }, // Left (-X)
            { 1, 5, 6, 2 }, // Right (+X)
            { 3, 2, 6, 7 }, // Top (+Y)
            { 4, 5, 1, 0 }  // Bottom (-Y)
        };

        if ((faceColor & 0xFF000000) != 0) {
            for (int f = 0; f < 6; ++f) {
                int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
                if (valid[i0] && valid[i1] && valid[i2] && valid[i3]) {
                    drawList->AddQuadFilled(
                        ImVec2(s[i0].x, s[i0].y),
                        ImVec2(s[i1].x, s[i1].y),
                        ImVec2(s[i2].x, s[i2].y),
                        ImVec2(s[i3].x, s[i3].y),
                        faceColor
                    );
                }
            }
        }

        // 12 Edges
        static const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Front face
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Back face
            {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Connecting edges
        };

        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0];
            int b = edges[e][1];
            if (valid[a] && valid[b]) {
                drawList->AddLine(
                    ImVec2(s[a].x, s[a].y),
                    ImVec2(s[b].x, s[b].y),
                    outlineColor,
                    lineThickness
                );
            }
        }

        // Coordinate tag on the highest visible corner
        int topCorner = 6; // maxPos
        if (valid[topCorner]) {
            char tag[64];
            snprintf(tag, sizeof(tag), "Box [%.0f, %.0f, %.0f]", minPos.x, minPos.y, minPos.z);
            drawList->AddText(ImVec2(s[topCorner].x + 4.0f, s[topCorner].y - 14.0f), outlineColor, tag);
        }
    }

    void DrawWaypointPillar(
        ImDrawList* drawList,
        const Vector3& blockPos,
        const float* vpMatrix,
        float screenWidth,
        float screenHeight,
        ImU32 beamColor,
        const char* label,
        ImFont* font
    ) {
        if (!drawList || !vpMatrix) return;

        // Pillar bottom (block surface) and top (sky)
        Vector3 botPos = Vector3(blockPos.x + 0.5f, blockPos.y, blockPos.z + 0.5f);
        Vector3 topPos = Vector3(blockPos.x + 0.5f, blockPos.y + 120.0f, blockPos.z + 0.5f);

        Vector2 sBot, sTop;
        bool bBot = WorldToScreen(botPos, vpMatrix, screenWidth, screenHeight, sBot);
        bool bTop = WorldToScreen(topPos, vpMatrix, screenWidth, screenHeight, sTop);

        // Draw glowing translucent outer beam & inner core beam
        if (bBot || bTop) {
            Vector2 p1 = bBot ? sBot : Vector2(sTop.x, screenHeight + 20.0f);
            Vector2 p2 = bTop ? sTop : Vector2(sBot.x, -20.0f);

            // Wide outer glow
            ImU32 outerCol = (beamColor & 0x00FFFFFF) | 0x33000000;
            drawList->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), outerCol, 14.0f);

            // Medium beam
            ImU32 midCol = (beamColor & 0x00FFFFFF) | 0x88000000;
            drawList->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), midCol, 6.0f);

            // Core bright white line
            drawList->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), IM_COL32(255, 255, 255, 230), 2.0f);
        }

        // Floating pill label at waypoint base
        if (bBot && label && label[0] != '\0') {
            float fontSize = 14.0f;
            ImVec2 textSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label) : ImGui::CalcTextSize(label);
            float padX = 10.0f;
            float padY = 5.0f;
            ImVec2 pillMin = ImVec2(sBot.x - textSize.x * 0.5f - padX, sBot.y - 32.0f - padY);
            ImVec2 pillMax = ImVec2(sBot.x + textSize.x * 0.5f + padX, sBot.y - 32.0f + textSize.y + padY);

            // Background pill
            drawList->AddRectFilled(pillMin, pillMax, IM_COL32(15, 20, 28, 235), 6.0f);
            drawList->AddRect(pillMin, pillMax, beamColor, 6.0f, 0, 1.5f);

            // Text
            if (font) {
                drawList->AddText(font, fontSize, ImVec2(sBot.x - textSize.x * 0.5f, sBot.y - 32.0f), IM_COL32(255, 255, 255, 255), label);
            } else {
                drawList->AddText(ImVec2(sBot.x - textSize.x * 0.5f, sBot.y - 32.0f), IM_COL32(255, 255, 255, 255), label);
            }
        }
    }
}
