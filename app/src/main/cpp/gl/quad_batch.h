#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <vector>

#include "shader.h"

namespace kiosk::gl {

struct Color {
    float r, g, b, a;
    static Color rgba(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
    static Color rgb8(int r, int g, int b, float a = 1.0f) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a};
    }
};

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Batches textured quads (position in screen-space pixels, top-left origin,
// y-down -- matching the LayoutEngine's coordinate system) into as few
// glDrawArrays calls as possible. A batch is flushed whenever the bound
// texture or active clip rect changes, or at end-of-frame. Quads sharing a
// texture and clip region (e.g. all the solid-color chrome, or an entire row
// of already-loaded thumbnails before/after a clip change) are coalesced.
class QuadBatch {
public:
    bool init();
    void destroy();

    // Call once per frame before any push*; sets up the orthographic
    // projection for the given framebuffer size.
    void begin(int screenWidthPx, int screenHeightPx);

    // Flushes any pending quads and issues the final draw call.
    void end();

    void pushSolidRect(const Rect& rect, const Color& color);
    void pushTexturedRect(const Rect& rect, float u0, float v0, float u1, float v1,
                           GLuint texture, const Color& tint = {1, 1, 1, 1});

    // Quads pushed after setClip are scissored to `rect` (intersected with
    // any currently active clip). clearClip restores the previous clip
    // (nested push/pop), and clearAllClips disables scissoring entirely.
    void pushClip(const Rect& rect);
    void popClip();

    GLuint whiteTexture() const { return whiteTexture_; }

private:
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    void flush();
    void ensureCapacity(size_t additionalVerts);
    Rect currentClip() const;

    ShaderProgram shader_;
    GLuint vbo_ = 0;
    GLuint whiteTexture_ = 0;

    std::vector<Vertex> vertices_;
    GLuint currentTexture_ = 0;
    bool haveCurrentTexture_ = false;

    std::vector<Rect> clipStack_;

    int screenW_ = 0, screenH_ = 0;
    float projection_[16] = {};
};

} // namespace kiosk::gl
