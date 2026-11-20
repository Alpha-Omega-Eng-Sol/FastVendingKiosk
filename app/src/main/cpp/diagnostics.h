#pragma once

#include <chrono>
#include <cstdint>

namespace kiosk {

// Lightweight running performance counters. All timings here are CPU-side
// (wall-clock time spent building the frame's draw list and issuing GL
// calls / eglSwapBuffers on this thread); there is no GPU execution timing
// since that needs EGL/GLES extensions (e.g. EGL_ANDROID_get_frame_timestamps
// or GL timer queries) that are not guaranteed available on a GLES 2.0 /
// RK3568-class device, and we'd rather report nothing than an invented
// number. Logged periodically (not every frame) to avoid log spam.
class Diagnostics {
public:
    void beginFrame() { frameStart_ = std::chrono::steady_clock::now(); }

    void endFrame(int visibleCards, size_t decodedCacheBytes, size_t textureCount) {
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - frameStart_).count();

        frameCount_++;
        totalFrameTimeMs_ += ms;
        if (ms > maxFrameTimeMs_) maxFrameTimeMs_ = ms;
        lastVisibleCards_ = visibleCards;
        lastDecodedCacheBytes_ = decodedCacheBytes;
        lastTextureCount_ = textureCount;

        if (loggingEnabled_ &&
            std::chrono::duration<double>(now - lastLog_).count() >= kLogIntervalSeconds) {
            logSummary();
            lastLog_ = now;
        }
    }

    void setLoggingEnabled(bool enabled) { loggingEnabled_ = enabled; }

    double averageFrameTimeMs() const {
        return frameCount_ > 0 ? totalFrameTimeMs_ / frameCount_ : 0.0;
    }
    double maxFrameTimeMs() const { return maxFrameTimeMs_; }
    double approxFps() const {
        double avg = averageFrameTimeMs();
        return avg > 0.0 ? 1000.0 / avg : 0.0;
    }
    uint64_t frameCount() const { return frameCount_; }

private:
    static constexpr double kLogIntervalSeconds = 5.0;

    void logSummary() const;

    std::chrono::steady_clock::time_point frameStart_;
    std::chrono::steady_clock::time_point lastLog_ = std::chrono::steady_clock::now();
    uint64_t frameCount_ = 0;
    double totalFrameTimeMs_ = 0.0;
    double maxFrameTimeMs_ = 0.0;
    int lastVisibleCards_ = 0;
    size_t lastDecodedCacheBytes_ = 0;
    size_t lastTextureCount_ = 0;
    bool loggingEnabled_ = true;
};

} // namespace kiosk
