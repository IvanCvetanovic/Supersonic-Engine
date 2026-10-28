#pragma once

#include <cstdint>
#include <type_traits>

namespace Supersonic {

// A Vulkan handle as a plain 64-bit number, and back again.
//
// A non-dispatchable handle (VkDescriptorSet, VkBuffer, VkImage and the rest)
// is a pointer to an opaque struct on a 64-bit build and a uint64_t on a
// 32-bit one (VK_DEFINE_NON_DISPATCHABLE_HANDLE), so a reinterpret_cast is
// ill-formed on one of the two, and a cast through uintptr_t compiles on both
// but keeps only the low half of the 32-bit build's handle. These two look at
// the type instead: a pointer goes through uintptr_t, an integer is widened as
// it is, and the same bits come back out. The number is what ImGui's
// ImTextureID (64 bits on every build) and the batcher's draw keys hold.
template <class Handle>
uint64_t HandleToU64(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
    } else {
        static_assert(std::is_integral_v<Handle>, "a Vulkan handle is a pointer or an integer");
        return static_cast<uint64_t>(handle);
    }
}

template <class Handle>
Handle HandleFromU64(uint64_t value) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(static_cast<uintptr_t>(value));
    } else {
        static_assert(std::is_integral_v<Handle>, "a Vulkan handle is a pointer or an integer");
        return static_cast<Handle>(value);
    }
}

} // namespace Supersonic
