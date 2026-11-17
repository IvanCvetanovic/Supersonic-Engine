#pragma once

#include <cstring>
#include <optional>

#include "imgui.h"

namespace Supersonic {

// The screen overlay drawn on the window rather than into the picture
// (ScreenOverlay::Placement), placed as a draw callback in ImGui's draw list
// right after the image that shows the picture.
//
// A callback, not a pass of its own: the quads then land between the picture
// and whatever ImGui draws after it - a game's UICanvas, a later window such as
// the performance readout - in the order a HUD was always seen in, and they go
// into the swapchain pass that already loads and stores that image. The
// renderer records them when ImGui's backend reaches the callback.
//
// ImGui only; nothing here names Vulkan, so a suite holds the placement with a
// headless context.

// What the callback is handed: the renderer that records the frame, and where
// the picture is shown, in ImGui's coordinates. Copied into the draw list by
// value (AddCallback with a size), so nothing in it has to outlive the frame
// but the renderer.
struct WindowOverlayCall {
    void* renderer{nullptr};
    float imageMin[2]{0.0f, 0.0f};
    float imageMax[2]{0.0f, 0.0f};
};

// Adds `draw` carrying the call, then the backend's reset of its own state.
//
// The reset is not optional. The overlay binds its own pipeline, its own push
// constants and its material set at set 1, and the Vulkan backend keeps its
// sampler at set 1 (imgui_impl_vulkan.cpp, ImGui_ImplVulkan_SetupRenderState):
// without the reset, ImGui's next draw would sample through the overlay's set
// with the overlay's pipeline bound. With no reset registered - no backend, or
// one that offers none - nothing is added and the answer is false, and the
// overlay goes into the picture as it always did.
inline bool PlaceWindowOverlay(ImDrawList& list, ImDrawCallback draw, void* renderer, ImVec2 min, ImVec2 max) {
    const ImDrawCallback reset = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
    if (draw == nullptr || reset == nullptr) return false;
    WindowOverlayCall call;
    call.renderer = renderer;
    call.imageMin[0] = min.x;
    call.imageMin[1] = min.y;
    call.imageMax[0] = max.x;
    call.imageMax[1] = max.y;
    list.AddCallback(draw, &call, sizeof call);
    list.AddCallback(reset, nullptr);
    return true;
}

// The call one command carries, or none when it carries no call of this size.
//
// Copied out rather than pointed at: the copy sits in a byte buffer of its draw
// list, after whatever data earlier callbacks of that list stored, so it need
// not be aligned for the pointer at its start.
inline std::optional<WindowOverlayCall> WindowOverlayCallOf(const ImDrawCmd& cmd) {
    if (cmd.UserCallbackData == nullptr ||
        cmd.UserCallbackDataSize != static_cast<int>(sizeof(WindowOverlayCall))) {
        return std::nullopt;
    }
    WindowOverlayCall call;
    std::memcpy(&call, cmd.UserCallbackData, sizeof call);
    return call;
}

// The call placed with `draw` in this frame's draw data, or none.
//
// Valid only after ImGui::Render: a callback's copied data has no address until
// Render resolves it (ImDrawCmd::UserCallbackData is null before), so asked any
// earlier this answers none.
inline std::optional<WindowOverlayCall> FindWindowOverlay(const ImDrawData* drawData, ImDrawCallback draw) {
    if (drawData == nullptr || draw == nullptr) return std::nullopt;
    for (const ImDrawList* list : drawData->CmdLists) {
        if (list == nullptr) continue;
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != draw) continue;
            if (std::optional<WindowOverlayCall> call = WindowOverlayCallOf(cmd)) return call;
        }
    }
    return std::nullopt;
}

} // namespace Supersonic
