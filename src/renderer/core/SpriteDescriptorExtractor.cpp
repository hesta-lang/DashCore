#include "renderer/core/SpriteDescriptorExtractor.hpp"

namespace dashcore::renderer::core {

bool extractSpriteDescriptor(
    RawSpriteAttributes const& attributes,
    ObjectId owner,
    std::uint32_t childIndex,
    LayerOrder order,
    SpriteDescriptor& output
) noexcept {
    SpriteDescriptor candidate{};
    candidate.owner = owner;
    candidate.id = {owner, childIndex};
    candidate.order = order;
    candidate.attributes = attributes;

    if (!validateSpriteDescriptor(candidate)) {
        output = {};
        return false;
    }

    output = candidate;
    return true;
}

} // namespace dashcore::renderer::core
