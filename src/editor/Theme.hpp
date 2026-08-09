#pragma once

#include "imgui.h"
#include <glm/glm.hpp>
#include <string>

namespace Engine {

class Theme {
public:
    static void ApplyEngineDarkTheme();
    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);
};

} // namespace Engine
