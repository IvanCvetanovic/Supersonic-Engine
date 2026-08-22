#pragma once

#include <cstddef>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Supersonic {

// Which lights the scene pass gets, when it cannot have all of them.
//
// The scene UBO holds a fixed array of eight, and the loop that filled it used
// to walk the registry and stop at the ninth. That made the choice an artefact
// of iteration order - and EnTT views iterate a pool in REVERSE insertion
// order, so the eight a scene got were the eight authored LAST. A level with
// twelve lamps in it dropped its sun, which is both the light that matters most
// and the only one the shader can shadow, because the sun was placed first.
//
// Lives here rather than in VulkanRenderer for two reasons. It needs nothing
// from Vulkan - it is a sort over components - and the suites deliberately
// touch no Vulkan entry point, so a rule about which lights survive could not
// otherwise be tested at all.
//
// This is NOT light culling. Nine lights in one small room still means one of
// them is dropped; what this decides is WHICH one, and it makes the drop
// predictable instead of incidental. Lifting the cap itself needs the froxel
// grid that the roadmap still lists as unshipped.

// Where a light actually is: the world matrix when the transform hierarchy has
// been resolved, the local transform before it. A lamp parented to a character
// is at the character, not at its own offset within them.
glm::vec3 LightWorldPosition(const entt::registry& registry, entt::entity entity);

// At most maxLights entities that have a LightComponent, most relevant first.
//
// Directional lights sort ahead of everything: they light the whole scene
// regardless of where they sit, they are the cheapest to evaluate, and the
// shadow-casting one has to survive to reach lights[0], the only slot the
// shader applies a shadow factor to.
//
// Local lights sort by how close their sphere of influence comes to the view
// position - distance to the EDGE of their range, not to the light. Ranking by
// raw distance would drop a wide lamp lighting the whole shot in favour of a
// pinpoint one nearer the camera that illuminates nothing.
//
// The order is stable among equals, so two lights the same distance out keep
// their registry order rather than swapping between frames as floats compare
// differently, which would flicker.
std::vector<entt::entity> SelectLights(const entt::registry& registry,
                                       const glm::vec3& viewPosition,
                                       std::size_t maxLights);

} // namespace Supersonic
