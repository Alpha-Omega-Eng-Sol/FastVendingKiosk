#include "image_manager.h"

#include "../gl/gl_common.h"

namespace kiosk {

namespace {
constexpr auto kFailureRetryCooldown = std::chrono::seconds(15);
}

ImageManager::ImageManager(size_t maxDecoded, size_t maxTextures)
    : maxDecoded_(maxDecoded), maxTextures_(maxTextures) {}

ImageManager::Entry& ImageManager::entryFor(long productId) {
    return entries_[productId]; // default-constructs on first access
}

bool ImageManager::needsRequest(long productId) {
    Entry& e = entryFor(productId);
    if (e.state == ImageState::NotRequested) return true;
    if (e.state == ImageState::Failed) {
        return std::chrono::steady_clock::now() - e.failedAt > kFailureRetryCooldown;
    }
    return false;
}

void ImageManager::markRequested(long productId) {
    entryFor(productId).state = ImageState::Requested;
}

void ImageManager::onDecoded(long productId, int width, int height, std::vector<uint8_t> rgba) {
    Entry& e = entryFor(productId);
    if (e.inDecodedLru) {
        decodedBytes_ -= e.rgba.size();
    }
    e.width = width;
    e.height = height;
    e.rgba = std::move(rgba);
    e.state = ImageState::Decoded;
    decodedBytes_ += e.rgba.size();
    touchDecodedLru(productId, e);
    evictDecodedIfNeeded();
}

void ImageManager::onFailed(long productId) {
    Entry& e = entryFor(productId);
    e.state = ImageState::Failed;
    e.failedAt = std::chrono::steady_clock::now();
}

ImageState ImageManager::stateOf(long productId) const {
    auto it = entries_.find(productId);
    return it == entries_.end() ? ImageState::NotRequested : it->second.state;
}

void ImageManager::touchDecodedLru(long productId, Entry& e) {
    if (e.inDecodedLru) {
        decodedOrder_.erase(e.decodedLruIt);
    }
    decodedOrder_.push_front(productId);
    e.decodedLruIt = decodedOrder_.begin();
    e.inDecodedLru = true;
}

void ImageManager::touchTextureLru(long productId, Entry& e) {
    if (e.inTextureLru) {
        textureOrder_.erase(e.textureLruIt);
    }
    textureOrder_.push_front(productId);
    e.textureLruIt = textureOrder_.begin();
    e.inTextureLru = true;
}

void ImageManager::evictDecodedIfNeeded() {
    while (decodedOrder_.size() > maxDecoded_) {
        long victimId = decodedOrder_.back();
        decodedOrder_.pop_back();
        auto it = entries_.find(victimId);
        if (it == entries_.end()) continue;
        Entry& victim = it->second;
        victim.inDecodedLru = false;
        decodedBytes_ -= victim.rgba.size();
        victim.rgba.clear();
        victim.rgba.shrink_to_fit();
        if (victim.state == ImageState::Ready) dropTexture(victim);
        // Evicted decoded pixels without ever having failed/been requested
        // again -- treat as NotRequested so it can be re-fetched if it
        // becomes visible again later.
        victim.state = ImageState::NotRequested;
    }
}

void ImageManager::dropTexture(Entry& e) {
    if (e.texture) {
        glDeleteTextures(1, &e.texture);
        e.texture = 0;
    }
    if (e.inTextureLru) {
        textureOrder_.erase(e.textureLruIt);
        e.inTextureLru = false;
    }
}

void ImageManager::evictTextureIfNeeded() {
    while (textureOrder_.size() > maxTextures_) {
        long victimId = textureOrder_.back();
        auto it = entries_.find(victimId);
        if (it == entries_.end()) { textureOrder_.pop_back(); continue; }
        Entry& victim = it->second;
        dropTexture(victim);
        // Still has decoded pixels cached, so demote rather than discard.
        if (victim.state == ImageState::Ready) victim.state = ImageState::Decoded;
    }
}

GLuint ImageManager::textureFor(long productId) {
    auto it = entries_.find(productId);
    if (it == entries_.end()) return 0;
    Entry& e = it->second;

    if (e.state == ImageState::Ready && e.texture) {
        touchTextureLru(productId, e);
        return e.texture;
    }
    if (e.state != ImageState::Decoded || e.rgba.empty()) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, e.width, e.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    e.texture = tex;
    e.state = ImageState::Ready;
    touchTextureLru(productId, e);
    evictTextureIfNeeded();
    return tex;
}

void ImageManager::releaseAllTextures() {
    for (auto& [id, e] : entries_) {
        e.texture = 0; // context is already gone; the GL objects are invalid, not just leaked
        e.inTextureLru = false;
        if (e.state == ImageState::Ready) {
            e.state = e.rgba.empty() ? ImageState::NotRequested : ImageState::Decoded;
        }
    }
    textureOrder_.clear();
}

} // namespace kiosk
