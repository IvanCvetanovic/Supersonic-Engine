#pragma once

#include <string>
#include <vector>

#include "core/Json.hpp"
#include "sim/GameData.hpp"

namespace WolfBrigade {

// Cross-reference and shape checking for the game's data, so an authoring typo
// fails loudly instead of no-oping downstream.
//
// A port of `scripts/core/data_validator.gd`. Read-only and value-agnostic: it
// never asks whether a number is balanced, only whether an id refers to
// something that exists and whether a field is the shape the reader expects. A
// wave that spawns "raidder" is otherwise a wave that spawns nothing, on a
// device, after the file was committed.
//
// The GDScript exists in the shape it does because of a Godot hazard: `x as
// Dictionary` on a non-Dictionary THROWS, so validating the very mistake it
// exists to catch would crash the loader. This engine's Json::Value returns an
// empty container instead, so the crash cannot happen - but the REPORT still
// has to, or a field authored as a list where an object belongs is silently
// treated as empty and the check passes.
namespace DataValidator {

using Issues = std::vector<std::string>;

// Every problem in the loaded data. Empty means clean.
Issues Check(const GameData& data);

// Names in `refs` that are blank or absent from `validIds`.
//
// The building block every check above is made of, and pure so it can be tested
// on its own. A BLANK id counts as missing: an empty string in a reference
// field is a typo or a half-finished edit, not "this thing deliberately refers
// to nothing" - which is what an absent field or an empty list means.
Issues MissingRefs(const std::vector<std::string>& refs,
                   const std::vector<std::string>& validIds,
                   const std::string& context);

// The two type guards, exposed because the original's harness tests them
// directly - and because "returns empty and reports one issue" is a contract
// worth pinning rather than an implementation detail.
//
// Absence is NOT a problem here; only being present and the wrong shape is.
// Godot spells that `dict.get(key, {})`, where a missing key yields the empty
// default and never reaches the type check.
const Supersonic::Json::Object& AsObject(const Supersonic::Json::Value& value,
                                         const std::string& context, Issues& out);
const Supersonic::Json::Array& AsArray(const Supersonic::Json::Value& value,
                                       const std::string& context, Issues& out);

} // namespace DataValidator

} // namespace WolfBrigade
