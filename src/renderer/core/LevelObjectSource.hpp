#pragma once

#include "renderer/core/RendererTypes.hpp"

#include <cstdint>
#include <vector>

namespace dashcore::renderer::core {

// Source access is synchronous. Implementations may inspect engine objects
// during collect(), but must copy only value data into the returned descriptors.
class LevelObjectSource {
public:
    virtual ~LevelObjectSource() = default;

    virtual bool collect(
        std::uint32_t levelGeneration,
        std::vector<SpriteDescriptor>& output
    ) const = 0;
};

} // namespace dashcore::renderer::core
