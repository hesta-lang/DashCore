#pragma once

namespace dashcore::bismuth {
    // Runs once after the GLES context is active. This is a non-invasive
    // compute/SSBO capability test; it does not replace or modify GD rendering.
    void runGLESComputeProbeOnce();
}
