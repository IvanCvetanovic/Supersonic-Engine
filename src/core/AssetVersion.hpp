#pragma once

#include <string>

#include "core/Json.hpp"
#include "core/JsonInts.hpp"

namespace Supersonic {

// Format versioning for everything the engine writes and reads back.
//
// No on-disk format carried a version. A scene said {"Scene": "MainScene",
// "Entities": [...]} - a name, and nothing about the shape of what followed.
// Prefabs were a bare component object with no envelope at all, and materials
// and game.manifest the same.
//
// Forward compatibility rested entirely on ComponentCodec::Read's per-field
// defaults, which handle exactly one kind of change well - a field that was
// ADDED - and nothing else:
//
//   - a renamed key silently reverts to its default, so an authored value is
//     replaced by a plausible wrong one rather than reported missing;
//   - a changed unit loads as a number that is the right type and the wrong
//     size, which nothing can detect;
//   - a file written by a NEWER build loads with the new fields dropped, and
//     reports success.
//
// That last one is the case worth spending anything on. Loading a scene from a
// future version and silently discarding whatever it knew is how a user loses
// work by opening it in the wrong build and pressing Save.
//
// The cost of adding this now is near zero and rises with every scene anyone
// authors, which is the whole argument for doing it before there are any.
namespace AssetVersion {

// Bump when the on-disk shape changes in a way a reader must know about.
// Adding an optional field does NOT need a bump: the per-field defaults
// already cover that, and it is the only case they cover.
inline constexpr int kCurrent = 2;

// Absent means 0: everything written before this existed.
inline int Read(const Json::Value& root) {
    // asI32, not a cast: a version of 1e20 is a number a scene can say, and the cast
    // was undefined behaviour that gave INT_MIN - an "ancient" file that is migrated -
    // where saturating makes it what it plainly is, newer than this build.
    return asI32(root["Version"].AsNumber(0.0));
}

// A file from the future is refused rather than partially read. There is no
// safe way to load one - the fields this build does not know about are exactly
// the ones it would drop.
inline bool IsReadable(int version) { return version <= kCurrent; }

inline std::string TooNewMessage(const std::string& what, int version) {
    return what + " was written by a newer build (format version " +
           std::to_string(version) + "; this build reads up to " +
           std::to_string(kCurrent) + "). Refusing to load it rather than " +
           "dropping the parts it does not understand.";
}

// Where a real migration goes.
//
// Deliberately empty today, and that is not the same as unnecessary: the hook
// is the thing that has to exist before the first change needs it, because
// retrofitting one means guessing what unversioned files on disk contain.
//
// Note what is NOT here. ComponentCodec reads ParticleEmitter by sniffing
// whether the node is an object, because older scenes wrote a bare `true`.
// That could be expressed as a 0 -> 1 migration, but it is left where it is on
// purpose: a type sniff works on a file with no version field at all, and
// those exist. A migration keyed on a version cannot fix a file that predates
// versions unless it assumes every unversioned file is the oldest shape.
inline void Migrate(Json::Value& root, int fromVersion) {
    // 1 -> 2: the world ground plane became a per-scene setting, defaulting OFF.
    //
    // Every scene written before that was authored against an unconditional
    // solid plane at y = 0, and leaning on it was a perfectly reasonable thing
    // to do while the floor was free - a level needed a collider only under the
    // parts you could fall off. Reading one of those with the new default turns
    // it into everything falling out of the world, with no message, which is
    // the sort of break this hook exists to stop.
    //
    // So a scene from before the switch gets the plane it was authored against,
    // and a new one gets the default. Both are what their author meant.
    //
    // Only when the key is absent. A hand-edited file that predates the version
    // bump but already says what it wants is taken at its word.
    if (fromVersion < 2 && !root.Has("Physics")) {
        Json::Object physics;
        physics.insert_or_assign("GroundPlane", Json::Value(true));
        physics.insert_or_assign("GroundPlaneY", Json::Value(0.0));
        root.Set("Physics", Json::Value(std::move(physics)));
    }
}

} // namespace AssetVersion
} // namespace Supersonic
