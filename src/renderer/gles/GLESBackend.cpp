#include "renderer/gles/GLESBackend.hpp"

#include "renderer/core/GlesCapabilities.hpp"

// Geode's Android CCGL.h already supplies the GLES2 buffer API and
// remaps OES vertex-array symbols. Including GLES3/gl3.h here conflicts
// with those compatibility macros; newer entry points are resolved through EGL.

#if defined(__ANDROID__)
#include <EGL/egl.h>
#endif

#include <limits>
#include <string>

namespace dashcore::renderer::gles {
namespace {

#if defined(__ANDROID__)
constexpr unsigned int kGlNumExtensions = 0x821D;
constexpr int kMaximumExtensionCount = 8192;

struct ExtensionList {
    std::string values;
    bool available = false;
};

ExtensionList readCurrentContextExtensions(
    std::uint8_t majorVersion
) {
    ExtensionList result{};

    if (majorVersion >= 3) {
        // glGetString(GL_EXTENSIONS) is not valid in GLES 3.x. Resolve the
        // core glGetStringi entry point and enumerate the indexed extension list.
        using GetStringiFunction = unsigned char const* (*)(unsigned int, unsigned int);
        auto const getStringi = reinterpret_cast<GetStringiFunction>(
            eglGetProcAddress("glGetStringi")
        );

        GLint extensionCount = 0;
        glGetIntegerv(kGlNumExtensions, &extensionCount);
        if (!getStringi || extensionCount < 0 ||
            extensionCount > kMaximumExtensionCount) {
            return result;
        }

        result.available = true;
        for (GLint index = 0; index < extensionCount; ++index) {
            auto const* extension = getStringi(
                static_cast<unsigned int>(GL_EXTENSIONS),
                static_cast<unsigned int>(index)
            );
            if (!extension) {
                result.available = false;
                result.values.clear();
                return result;
            }
            if (!result.values.empty()) result.values.push_back(' ');
            result.values.append(reinterpret_cast<char const*>(extension));
        }
        return result;
    }

    auto const* legacyExtensions = glGetString(GL_EXTENSIONS);
    if (legacyExtensions) {
        result.values = reinterpret_cast<char const*>(legacyExtensions);
        result.available = true;
    }
    return result;
}
#endif

} // namespace

core::GlesCapabilities queryCurrentContextCapabilities() noexcept {
#if defined(__ANDROID__)
    try {
        if (eglGetCurrentContext() == EGL_NO_CONTEXT) return {};

        auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
        if (!version) return {};

        auto const baseCapabilities =
            core::parseGlesCapabilities(version, nullptr, true);
        if (!baseCapabilities.contextAvailable) return {};

        auto const extensionList =
            readCurrentContextExtensions(baseCapabilities.majorVersion);
        return core::parseGlesCapabilities(
            version,
            extensionList.available ? extensionList.values.c_str() : nullptr,
            true
        );
    } catch (...) {
        return {};
    }
#else
    return {};
#endif
}

bool GLESBackend::initialize() {
    release();

#if defined(__ANDROID__)
    m_capabilities = queryCurrentContextCapabilities();
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

    GLint previousArrayBuffer = 0;
    GLint previousElementArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementArrayBuffer);

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

    // Do not leave Cocos2d's global buffer bindings changed.
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));
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
