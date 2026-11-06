#pragma once
#include <string>
#include <vector>

namespace bismuth::android {
struct GLCapabilities {
    int majorVersion = 0;
    int minorVersion = 0;
    bool isOpenGLES = false;
    bool hasComputeShaders = false;
    bool hasShaderStorageBuffers = false;
    bool hasIndirectDraw = false;
    bool hasMultiDrawIndirect = false;
    bool hasTimerQueries = false;
    bool hasShaderInclude = false;
    bool hasSpirvSpecialization = false;
    std::string version;
    std::string renderer;
    std::string vendor;
    std::vector<std::string> extensions;

    bool supportsFullBismuthPipeline() const {
        return !isOpenGLES && (majorVersion > 4 || (majorVersion == 4 && minorVersion >= 3)) &&
               hasComputeShaders && hasShaderStorageBuffers &&
               hasMultiDrawIndirect && hasShaderInclude &&
               hasSpirvSpecialization;
    }
    bool supportsComputeCulling() const {
        return isOpenGLES && hasComputeShaders && hasShaderStorageBuffers;
    }
};
GLCapabilities queryGLCapabilities();
} // namespace bismuth::android
