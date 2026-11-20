#pragma once

#include <android/native_window.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "input/input_controller.h"

namespace kiosk {

enum class CommandType {
    SurfaceCreated,
    SurfaceChanged,
    SurfaceDestroyed,
    Pause,
    Resume,
    Shutdown,
    Touch,
    CatalogLoaded,
    CatalogFailed,
    ImageDecoded,
    ImageFailed,
};

// A plain tagged struct rather than std::variant: the command set is small,
// fixed, and most fields are simple scalars -- a variant would add ceremony
// without buying much here. Always pushed from a Kotlin-originated thread
// (UI thread for lifecycle/touch, a background executor thread for network
// results) and drained only by the render thread.
struct Command {
    CommandType type;

    ANativeWindow* window = nullptr; // SurfaceCreated
    int width = 0, height = 0;        // SurfaceChanged

    input::TouchAction touchAction{}; // Touch
    float x = 0, y = 0;                // Touch
    long touchTimeMs = 0;              // Touch (MotionEvent.eventTime, ms since boot)

    std::string text;                 // CatalogLoaded (json) / CatalogFailed (message)

    long productId = 0;               // ImageDecoded / ImageFailed
    int imageWidth = 0, imageHeight = 0;
    std::vector<uint8_t> imagePixels;
};

// Thread-safe MPSC queue: many producer threads (UI thread, network
// executors) push commands; only the render thread drains them, at the top
// of each loop iteration, never while holding the lock during actual GL
// work.
class CommandQueue {
public:
    void push(Command cmd) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(cmd));
        }
        cv_.notify_one();
    }

    // Moves all pending commands into `out`, blocking up to `timeoutMs` if
    // the queue is empty (so the render thread can sleep when idle instead
    // of busy-polling). Returns immediately if anything is already queued.
    void drainBlocking(std::vector<Command>& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !queue_.empty(); });
        }
        while (!queue_.empty()) {
            out.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
    }

    void wake() { cv_.notify_one(); }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
};

} // namespace kiosk
