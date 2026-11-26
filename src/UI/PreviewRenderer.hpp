#pragma once

#include <imgui.h>
#include <string>

namespace PreviewRenderer {

void SetStructurePath(const std::string &path, const std::string &format);
void Invalidate();
void RenderViewport(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize, float yaw,
                    float pitch, float zoom, float alpha);
bool HasMesh();
int GetQuadCount();

}
