#include "quad_batch.h"

#include <algorithm>
#include <cstring>

#include "gl_common.h"

namespace kiosk::gl {

namespace {

void orthoProjection(float out[16], float width, float height) {
    // Maps screen-space pixels (origin top-left, y-down) directly to clip
    // space, so layout code never has to think about NDC.
    std::fill(out, out + 16, 0.0f);
    out[0] = 2.0f / width;
    out[5] = -2.0f / height;
    out[10] = -1.0f;
    out[12] = -1.0f;
    out[13] = 1.0f;
    out[15] = 1.0f;
}

} // namespace

bool QuadBatch::init() {
    if (!shader_.build()) return false;

    glGenBuffers(1, &vbo_);

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    vertices_.reserve(1024);
    return whiteTexture_ != 0 && vbo_ != 0;
}

void QuadBatch::destroy() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (whiteTexture_) { glDeleteTextures(1, &whiteTexture_); whiteTexture_ = 0; }
    shader_.destroy();
}

void QuadBatch::begin(int screenWidthPx, int screenHeightPx) {
    screenW_ = screenWidthPx;
    screenH_ = screenHeightPx;
    orthoProjection(projection_, static_cast<float>(screenWidthPx), static_cast<float>(screenHeightPx));
    vertices_.clear();
    haveCurrentTexture_ = false;
    clipStack_.clear();

    glUseProgram(shader_.program());
    glUniformMatrix4fv(shader_.uniformProjection(), 1, GL_FALSE, projection_);
    glUniform1i(shader_.uniformTexture(), 0);
    glActiveTexture(GL_TEXTURE0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
}

void QuadBatch::end() {
    flush();
    glDisable(GL_SCISSOR_TEST);
}

Rect QuadBatch::currentClip() const {
    if (clipStack_.empty()) return Rect{0, 0, static_cast<float>(screenW_), static_cast<float>(screenH_)};
    Rect r = clipStack_.front();
    for (size_t i = 1; i < clipStack_.size(); ++i) {
        const Rect& c = clipStack_[i];
        float x0 = std::max(r.x, c.x);
        float y0 = std::max(r.y, c.y);
        float x1 = std::min(r.x + r.w, c.x + c.w);
        float y1 = std::min(r.y + r.h, c.y + c.h);
        r = Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    return r;
}

void QuadBatch::pushClip(const Rect& rect) {
    flush();
    clipStack_.push_back(rect);
}

void QuadBatch::popClip() {
    flush();
    if (!clipStack_.empty()) clipStack_.pop_back();
}

void QuadBatch::ensureCapacity(size_t additionalVerts) {
    if (vertices_.size() + additionalVerts > vertices_.capacity()) {
        flush();
    }
}

void QuadBatch::pushSolidRect(const Rect& rect, const Color& color) {
    pushTexturedRect(rect, 0.0f, 0.0f, 1.0f, 1.0f, whiteTexture_, color);
}

void QuadBatch::pushTexturedRect(const Rect& rect, float u0, float v0, float u1, float v1,
                                  GLuint texture, const Color& tint) {
    if (texture == 0) texture = whiteTexture_;
    if (haveCurrentTexture_ && texture != currentTexture_) {
        flush();
    }
    currentTexture_ = texture;
    haveCurrentTexture_ = true;
    ensureCapacity(6);

    float x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.w, y1 = rect.y + rect.h;
    Vertex v00{x0, y0, u0, v0, tint.r, tint.g, tint.b, tint.a};
    Vertex v10{x1, y0, u1, v0, tint.r, tint.g, tint.b, tint.a};
    Vertex v11{x1, y1, u1, v1, tint.r, tint.g, tint.b, tint.a};
    Vertex v01{x0, y1, u0, v1, tint.r, tint.g, tint.b, tint.a};

    vertices_.push_back(v00);
    vertices_.push_back(v10);
    vertices_.push_back(v11);
    vertices_.push_back(v00);
    vertices_.push_back(v11);
    vertices_.push_back(v01);
}

void QuadBatch::flush() {
    if (vertices_.empty()) return;

    Rect clip = currentClip();
    if (clip.w <= 0.0f || clip.h <= 0.0f) {
        // Fully clipped away -- drop this batch without drawing.
        vertices_.clear();
        haveCurrentTexture_ = false;
        return;
    }
    if (clipStack_.empty()) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        glEnable(GL_SCISSOR_TEST);
        // Convert from top-left/y-down layout space to GL's bottom-left origin.
        GLint sx = static_cast<GLint>(clip.x);
        GLint sy = static_cast<GLint>(static_cast<float>(screenH_) - (clip.y + clip.h));
        glScissor(sx, sy, static_cast<GLsizei>(clip.w), static_cast<GLsizei>(clip.h));
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                 vertices_.data(), GL_DYNAMIC_DRAW);

    GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(static_cast<GLuint>(shader_.attribPos()));
    glVertexAttribPointer(static_cast<GLuint>(shader_.attribPos()), 2, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(static_cast<GLuint>(shader_.attribUv()));
    glVertexAttribPointer(static_cast<GLuint>(shader_.attribUv()), 2, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(static_cast<GLuint>(shader_.attribColor()));
    glVertexAttribPointer(static_cast<GLuint>(shader_.attribColor()), 4, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<const void*>(offsetof(Vertex, r)));

    glBindTexture(GL_TEXTURE_2D, currentTexture_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));

    vertices_.clear();
    haveCurrentTexture_ = false;
}

} // namespace kiosk::gl
