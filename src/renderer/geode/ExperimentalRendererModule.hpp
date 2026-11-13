#pragma once

namespace dashcore::renderer::geode {
class RendererCoordinator;
RendererCoordinator& coordinator() noexcept;
}

namespace dashcore::renderer {

void initialize();

} // namespace dashcore::renderer
