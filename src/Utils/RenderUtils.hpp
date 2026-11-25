#pragma once

#include <imgui.h>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace RenderUtils {
    struct Vector3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vector3() = default;
        Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }

        float Length() const { return std::sqrt(x * x + y * y + z * z); }
        float Distance(const Vector3& o) const { return (*this - o).Length(); }
    };

    struct Vector2 {
        float x = 0.0f;
        float y = 0.0f;

        Vector2() = default;
        Vector2(float _x, float _y) : x(_x), y(_y) {}
    };

    struct Matrix4x4 {
        float m[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
    };

    // Camera and View-Projection matrix state
    class RenderContext {
    public:
        static RenderContext& Get() {
            static RenderContext instance;
            return instance;
        }

        void UpdateGameMatrix(const float* vpMatrix16, const Vector3& camPos);
        void SetCameraOrigin(const Vector3& camOrigin);
        Vector3 GetCameraOrigin() const;
        void GetActiveViewProjection(float* outMatrix16) const;
        Vector3 GetActiveCameraPosition() const;

        bool HasValidGameMatrix() const { return m_hasGameMatrix; }
        bool UseSyntheticCamera() const { return m_useSyntheticCamera; }
        void SetUseSyntheticCamera(bool val) { m_useSyntheticCamera = val; }

        // Synthetic camera controls for testing anywhere (main menu / in-game)
        void BuildSyntheticMatrix(float screenWidth, float screenHeight);
        Vector3& GetSyntheticCamPos() { return m_syntheticCamPos; }
        float& GetSyntheticYaw() { return m_syntheticYaw; }
        float& GetSyntheticPitch() { return m_syntheticPitch; }

    private:
        RenderContext();
        mutable std::mutex m_mutex;
        Matrix4x4 m_gameVPMatrix{};
        Vector3 m_gameCamPos{ 0.0f, 70.0f, 0.0f };
        Vector3 m_camOrigin{ 0.0f, 0.0f, 0.0f };
        bool m_hasGameMatrix = false;

        bool m_useSyntheticCamera = false;
        Matrix4x4 m_syntheticVPMatrix{};
        Vector3 m_syntheticCamPos{ 0.0f, 5.0f, -10.0f };
        float m_syntheticYaw = 0.0f;
        float m_syntheticPitch = -15.0f;
    };

    // World to Screen projection using 4x4 ViewProjection matrix
    bool WorldToScreen(const Vector3& worldPos, const float* vpMatrix, float screenWidth, float screenHeight, Vector2& outScreen);

    // Draws a 3D wireframe bounding box with optional translucent face fill
    void Draw3DWireframeBox(
        ImDrawList* drawList,
        const Vector3& minPos,
        const Vector3& maxPos,
        const float* vpMatrix,
        float screenWidth,
        float screenHeight,
        ImU32 outlineColor,
        ImU32 faceColor,
        float lineThickness = 2.0f
    );

    // Draws an in-world waypoint beacon pillar with floating label (like Flarial)
    void DrawWaypointPillar(
        ImDrawList* drawList,
        const Vector3& blockPos,
        const float* vpMatrix,
        float screenWidth,
        float screenHeight,
        ImU32 beamColor,
        const char* label,
        ImFont* font = nullptr
    );
}
