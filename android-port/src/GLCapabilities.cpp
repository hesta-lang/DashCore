#include "BismuthAndroid/GLCapabilities.hpp"
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#error "Bismuth Android capability probe must only be built for Android targets"
#endif
#include <algorithm>
#include <cstring>

namespace bismuth::android {
namespace {
std::string glString(GLenum name) {
    auto const* value = glGetString(name);
    return value ? reinterpret_cast<char const*>(value) : std::string{};
}
bool hasExtension(std::vector<std::string> const& extensions, char const* name) {
    return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}
void parseVersion(GLCapabilities& result) {
    auto const marker = result.version.find("OpenGL ES ");
    if (marker == std::string::npos) return;
    result.isOpenGLES = true;
    auto const start = marker + std::strlen("OpenGL ES ");
    if (start + 2 >= result.version.size()) return;
    auto const* text = result.version.c_str() + start;
    if (text[0] < '0' || text[0] > '9' || text[1] != '.') return;
    result.majorVersion = text[0] - '0';
    if (text[2] >= '0' && text[2] <= '9') result.minorVersion = text[2] - '0';
}
} // namespace

GLCapabilities queryGLCapabilities() {
    GLCapabilities result;
    result.version = glString(GL_VERSION);
    result.renderer = glString(GL_RENDERER);
    result.vendor = glString(GL_VENDOR);
    parseVersion(result);
    if (!result.isOpenGLES || result.majorVersion < 3) return result;

    GLint extensionCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    if (extensionCount > 0 && extensionCount < 4096) {
        result.extensions.reserve(static_cast<std::size_t>(extensionCount));
        for (GLint i = 0; i < extensionCount; ++i) {
            auto const* extension = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (extension) result.extensions.emplace_back(reinterpret_cast<char const*>(extension));
        }
    }

    auto const es31OrNewer =
        result.majorVersion > 3 || (result.majorVersion == 3 && result.minorVersion >= 1);
    result.hasComputeShaders = es31OrNewer ||
        hasExtension(result.extensions, "GL_ARB_compute_shader");
    result.hasShaderStorageBuffers = es31OrNewer ||
        hasExtension(result.extensions, "GL_EXT_shader_io_blocks");

    result.hasIndirectDraw =
        hasExtension(result.extensions, "GL_EXT_draw_indirect") ||
        hasExtension(result.extensions, "GL_NV_draw_indirect");
    result.hasMultiDrawIndirect =
        hasExtension(result.extensions, "GL_EXT_multi_draw_indirect") ||
        hasExtension(result.extensions, "GL_NV_bindless_multi_draw_indirect");
    result.hasTimerQueries =
        hasExtension(result.extensions, "GL_EXT_disjoint_timer_query") ||
        hasExtension(result.extensions, "GL_EXT_disjoint_timer_query_webgl2");

    // Desktop-only paths from upstream Bismuth are not assumed on GLES.
    result.hasShaderInclude = false;
    result.hasSpirvSpecialization = false;
    return result;
}
} // namespace bismuth::android
