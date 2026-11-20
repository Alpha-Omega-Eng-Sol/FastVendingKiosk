#pragma once

#include <GLES2/gl2.h>
#include <chrono>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace kiosk {

enum class ImageState { NotRequested, Requested, Decoded, Ready, Failed };

// Owns, per product id: a bounded LRU cache of decoded RGBA pixel buffers,
// and a smaller bounded LRU cache of GL textures lazily created from them.
// Decoding happens off-thread in Kotlin (BitmapFactory); everything in this
// class that touches GL must run on the render thread, since GL textures may
// only be created/destroyed there.
//
// Two separate bounds exist because decoded pixels are cheap to keep around
// (a few hundred KB for the whole catalog) and let us survive EGL context
// loss without re-fetching over the network, while GPU textures are the
// scarcer resource and are bounded to roughly what's ever visible at once.
class ImageManager {
public:
    explicit ImageManager(size_t maxDecoded = 200, size_t maxTextures = 60);

    // Returns true the first time a product is asked about (caller should
    // then issue a network request), and false on every subsequent call
    // until the request resolves or a cooldown elapses after a failure.
    bool needsRequest(long productId);
    void markRequested(long productId);

    // Called with a freshly-decoded thumbnail (already downsampled on the
    // Kotlin side). Safe to call from the render thread only (it does not
    // touch GL itself, but is expected to be invoked while draining the
    // command queue there, same as everything else touching shared state).
    void onDecoded(long productId, int width, int height, std::vector<uint8_t> rgba);
    void onFailed(long productId);

    // GL-thread only. Returns a valid texture if one exists or could be
    // lazily created from cached decoded pixels; 0 otherwise (caller should
    // draw a placeholder).
    GLuint textureFor(long productId);

    ImageState stateOf(long productId) const;

    // Drops all GL textures (e.g. on EGL context loss) without discarding
    // decoded pixels, so textures can be cheaply recreated without
    // re-downloading anything.
    void releaseAllTextures();

    size_t decodedCacheBytesApprox() const { return decodedBytes_; }
    size_t textureCount() const { return textureOrder_.size(); }

private:
    struct Entry {
        ImageState state = ImageState::NotRequested;
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
        GLuint texture = 0;
        std::chrono::steady_clock::time_point failedAt{};
        std::list<long>::iterator decodedLruIt;
        bool inDecodedLru = false;
        std::list<long>::iterator textureLruIt;
        bool inTextureLru = false;
    };

    Entry& entryFor(long productId);
    void touchDecodedLru(long productId, Entry& e);
    void touchTextureLru(long productId, Entry& e);
    void evictDecodedIfNeeded();
    void evictTextureIfNeeded();
    void dropTexture(Entry& e);

    size_t maxDecoded_;
    size_t maxTextures_;
    size_t decodedBytes_ = 0;

    std::unordered_map<long, Entry> entries_;
    std::list<long> decodedOrder_; // front = most recently used
    std::list<long> textureOrder_;
};

} // namespace kiosk
