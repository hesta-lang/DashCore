#pragma once

#include "renderer/core/GeometryBuilder.hpp"
#include "renderer/core/GlesCapabilities.hpp"

namespace dashcore::renderer::core {

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual bool initialize() = 0;
    virtual bool prepare(GeometryData const& geometry) = 0;
    virtual void release() noexcept = 0;
    virtual GlesCapabilities capabilities() const noexcept = 0;

    // Deliberately no draw(), present(), or framebuffer replacement entry point.
};

} // namespace dashcore::renderer::core
