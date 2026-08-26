#include "sim/ResourceNode.hpp"

#include <algorithm>

namespace WolfBrigade {

int ResourceNode::Extract(int n) {
    // Never more than is there, and never a negative bite: a caller asking for
    // -5 would otherwise ADD five to the node and hand back a negative load
    // that the worker then banks as a debt.
    const int got = std::max(0, std::min(n, amount));
    amount -= got;
    return got;
}

Supersonic::Json::Value ResourceNode::ToSave() const {
    Supersonic::Json::Object out;
    out["resource"] = Supersonic::Json::Value(resource);
    out["max_amount"] = Supersonic::Json::Value(static_cast<double>(maxAmount));
    out["amount"] = Supersonic::Json::Value(static_cast<double>(amount));

    Supersonic::Json::Array size;
    size.push_back(Supersonic::Json::Value(static_cast<double>(bodySize.x)));
    size.push_back(Supersonic::Json::Value(static_cast<double>(bodySize.y)));
    out["size"] = Supersonic::Json::Value(std::move(size));

    out["color"] = Supersonic::Json::Value(color);

    Supersonic::Json::Array pos;
    pos.push_back(Supersonic::Json::Value(static_cast<double>(position.x)));
    pos.push_back(Supersonic::Json::Value(static_cast<double>(position.y)));
    out["pos"] = Supersonic::Json::Value(std::move(pos));
    return Supersonic::Json::Value(std::move(out));
}

ResourceNode ResourceNode::FromSave(const Supersonic::Json::Value& saved) {
    ResourceNode node;
    node.resource = saved["resource"].AsString(Ids::kWood);
    node.maxAmount = static_cast<int>(saved["max_amount"].AsNumber(200.0));

    // The saved amount, not the maximum. Setting it from max is how a
    // half-harvested forest comes back full, which is a free 40% of the
    // economy on every reload.
    node.amount = static_cast<int>(saved["amount"].AsNumber(static_cast<double>(node.maxAmount)));

    const auto& size = saved["size"].AsArray();
    if (size.size() >= 2) node.bodySize = glm::vec2(size[0].AsFloat(), size[1].AsFloat());
    node.color = saved["color"].AsString("#3f6b34");

    const auto& pos = saved["pos"].AsArray();
    if (pos.size() >= 2) node.position = glm::vec2(pos[0].AsFloat(), pos[1].AsFloat());
    return node;
}

float ResourceNode::Fraction() const {
    const float total = static_cast<float>(std::max(maxAmount, 1));
    return std::min(1.0f, std::max(0.0f, static_cast<float>(amount) / total));
}

} // namespace WolfBrigade
