#pragma once

#include <string>

#include <glm/glm.hpp>

#include "core/Json.hpp"
#include "sim/GameData.hpp"

namespace WolfBrigade {

// A harvestable pile - a tree, a berry bush - from `scripts/entities/resource_node.gd`.
//
// Holds an integer amount and hands it out in bites. Workers find it by asking
// which nodes are still harvestable and pull from it with Extract.
//
// The Godot version is a Node2D that shrinks its body as it depletes and frees
// itself when empty. Neither of those is here: the shrinking is presentation
// and belongs with whatever draws it, and the freeing is a scene-tree
// mechanism. What survives the port is the part with a decision in it - how
// much comes out, and when the node stops being a target.
struct ResourceNode {
    // Which resource this produces. An id from the economy file, not an enum:
    // the set of resources is whatever the data says it is.
    std::string resource{Ids::kWood};

    int maxAmount{200};
    int amount{200};

    glm::vec2 position{0.0f};

    // Authored, and carried through the save so a node rebuilt from one looks
    // like the node that was saved rather than a default tree.
    glm::vec2 bodySize{44.0f, 96.0f};
    std::string color{"#3f6b34"};

    // The art, a res:// path from the economy file, or "" for the flat colour.
    // Carried through the save because a restore bypasses the spawn that set
    // it. Display only: nothing in the simulation reads it.
    std::string sprite;

    bool IsEmpty() const { return amount <= 0; }

    // Whether a worker may target this.
    //
    // The original expresses it as scene-tree group membership, and leaves the
    // group the instant the node empties rather than when it finishes fading
    // out - so no worker walks to a tree that is already gone. Here it is the
    // same question asked directly.
    bool Harvestable() const { return amount > 0; }

    // Removes up to `n` and returns what actually came out.
    //
    // Clamped to what is left, which is the whole reason it returns a number: a
    // worker asking for a full carry-load from a nearly-empty tree gets what is
    // there, and the difference between asked and got is what conserves the
    // total. A version that returned `n` and let the amount go negative would
    // mint wood.
    int Extract(int n);

    // How full it is, 0..1. The visual reads this; nothing else does.
    float Fraction() const;

    // Every field, including the two the original's own equivalence digest
    // never compares - the colour and the body size. A node rebuilt without
    // them looks like a default tree rather than like the node that was saved,
    // and no round-trip check would notice.
    Supersonic::Json::Value ToSave() const;
    static ResourceNode FromSave(const Supersonic::Json::Value& saved);

    // The sprite the economy file gives this resource today, or "". A save
    // written before art existed carries none, and a restore adopts this
    // rather than bringing a lone flat rectangle back into a world of sprites:
    // the save carries an override, and the data stays the truth.
    static std::string SpriteFor(const GameData& data, const std::string& resource);
};

} // namespace WolfBrigade
