#pragma once

#include <ostream>
#include <string>

#include <entt/entt.hpp>

#include "core/Json.hpp"

namespace Supersonic {

// The single reader and writer for an entity's components.
//
// Scenes and prefabs each had their own, in different formats, covering
// different subsets: a prefab carried five components while a scene carried
// fifteen, so saving an entity as a prefab silently dropped its scripts, its
// lights, its audio, its animator and every collider but the box.
//
// Two writers over the same data will always drift - the only question is which
// component gets forgotten next, and how long before anyone notices. There is
// one now, and adding a component means editing one place.
namespace ComponentCodec {

// Writes every component the entity has, as JSON object members at the given
// indentation. Does NOT write the enclosing braces, and does not write the
// parent link: that is an index into a scene's entity array and means nothing
// to a standalone prefab.
//
// The final member is emitted without a trailing comma, so the caller can close
// the object immediately.
void Write(entt::registry& registry, entt::entity entity, std::ostream& out,
           const std::string& indent);

// Applies whatever the node describes onto an existing entity. Components the
// node does not mention are left alone, so this can be layered over defaults.
//
// Layering is the documented contract, and every branch used plain emplace,
// which asserts in a debug build when the component is already there and is
// undefined behaviour in a release one. It survived because the two callers
// that matter - loading a scene and instantiating a prefab - both start from a
// fresh entity. Applying a prefab over an existing entity, which is what the
// contract above promises, hit it immediately.
void Read(entt::registry& registry, entt::entity entity, const Json::Value& node);

} // namespace ComponentCodec

} // namespace Supersonic
