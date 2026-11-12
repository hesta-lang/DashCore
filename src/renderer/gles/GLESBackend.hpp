#pragma once

#include "renderer/core/RenderBackend.hpp"

#include <cstdint>

namespace dashcore::renderer::gles {

class GLESBackend final : public core::RenderBackend {
public:
    bool initialize() override;
    bool prepare(core::GeometryData const& geometry) override;
    void release() noexcept override;
    core::GlesCapabilities capabilities() const noexcept override;

private:
    core::GlesCapabilities m_capabilities{};
    std::uint32_t m_vertexBuffer = 0;
    std::uint32_t m_indexBuffer = 0;
    bool m_ready = false;
};

} // namespace dashcore::renderer::gles
