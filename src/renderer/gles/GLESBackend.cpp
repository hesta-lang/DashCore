#include "renderer/gles/GLESBackend.hpp"

#include "renderer/core/GlesCapabilities.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#endif

#include <limits>

namespace dashcore::renderer::gles {

bool GLESBackend::initialize() {
    release();

#if defined(__ANDROID__)
    auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    auto const* extensions = reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS));
    m_capabilities = core::parseGlesCapabilities(version, extensions, version != nullptr);
    if (!m_capabilities.contextAvailable || !m_capabilities.supportsBufferObjects) {
        return false;
    }

    GLuint buffers[2] = {0, 0};
    glGenBuffers(2, buffers);
    if (buffers[0] == 0 || buffers[1] == 0) {
        if (buffers[0] != 0 || buffers[1] != 0) glDeleteBuffers(2, buffers);
        return false;
    }

    m_vertexBuffer = buffers[0];
    m_indexBuffer = buffers[1];
    m_ready = true;
    return true;
#else
    m_capabilities = {};
    return false;
#endif
}

bool GLESBackend::prepare(core::GeometryData const& geometry) {
#if defined(__ANDROID__)
    if (!m_ready) return false;
    if (geometry.vertices.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(core::Vertex) ||
        geometry.indices.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(std::uint32_t)) {
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(geometry.vertices.size() * sizeof(core::Vertex)),
        geometry.vertices.empty() ? nullptr : geometry.vertices.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(geometry.indices.size() * sizeof(std::uint32_t)),
        geometry.indices.empty() ? nullptr : geometry.indices.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
#else
    (void)geometry;
    return false;
#endif
}

void GLESBackend::release() noexcept {
#if defined(__ANDROID__)
    GLuint buffers[2] = {
        static_cast<GLuint>(m_vertexBuffer),
        static_cast<GLuint>(m_indexBuffer)
    };
    if (buffers[0] != 0 || buffers[1] != 0) glDeleteBuffers(2, buffers);
#endif
    m_vertexBuffer = 0;
    m_indexBuffer = 0;
    m_ready = false;
    m_capabilities = {};
}

core::GlesCapabilities GLESBackend::capabilities() const noexcept {
    return m_capabilities;
}

} // namespace dashcore::renderer::gles
