#include "diagnostics.h"

#include "gl/gl_common.h"

namespace kiosk {

void Diagnostics::logSummary() const {
    LOGI("perf: frames=%llu avgFrame=%.2fms maxFrame=%.2fms fps~=%.1f visibleCards=%d "
         "imageCacheBytes~=%zu gpuTextures=%zu [CPU submission time only, no GPU timestamp]",
         (unsigned long long)frameCount_, averageFrameTimeMs(), maxFrameTimeMs_, approxFps(),
         lastVisibleCards_, lastDecodedCacheBytes_, lastTextureCount_);
}

} // namespace kiosk
