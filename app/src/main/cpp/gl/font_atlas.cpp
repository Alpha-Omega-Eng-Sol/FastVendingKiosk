#include "font_atlas.h"

#include "gl_common.h"

namespace kiosk::gl {

namespace {
constexpr int kFirstChar = 32;
constexpr int kNumChars = 96; // ASCII 32..127, covers titles/prices/currency/punctuation
constexpr int kAtlasW = 512;
constexpr int kAtlasH = 256;

// Base sizes in dp; multiplied by display density at build() time so text
// stays legible across a phone screen and a larger kiosk panel.
float baseDpForSize(FontSize size) {
    switch (size) {
        case FontSize::Small: return 11.0f;
        case FontSize::Regular: return 14.0f;
        case FontSize::Large: return 18.0f;
        case FontSize::Header: return 22.0f;
        default: return 14.0f;
    }
}
} // namespace

bool FontAtlas::build(AAssetManager* assetManager, float density) {
    destroy();

    AAsset* asset = AAssetManager_open(assetManager, "fonts/Roboto-Regular.ttf", AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open fonts/Roboto-Regular.ttf from assets");
        return false;
    }
    off_t length = AAsset_getLength(asset);
    fontBuffer_.resize(static_cast<size_t>(length));
    int read = AAsset_read(asset, fontBuffer_.data(), static_cast<size_t>(length));
    AAsset_close(asset);
    if (read != length) {
        LOGE("Short read on font asset: got %d of %lld bytes", read, static_cast<long long>(length));
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < atlases_.size(); ++i) {
        float px = baseDpForSize(static_cast<FontSize>(i)) * density;
        ok = bakeSize(fontBuffer_.data(), px, &atlases_[i]) && ok;
    }
    built_ = ok;
    if (!ok) LOGE("FontAtlas: one or more sizes failed to bake");
    return ok;
}

bool FontAtlas::bakeSize(const unsigned char* fontData, float pixelHeight, SizeAtlas* out) {
    std::vector<unsigned char> bitmap(static_cast<size_t>(kAtlasW) * kAtlasH);
    int result = stbtt_BakeFontBitmap(fontData, 0, pixelHeight, bitmap.data(), kAtlasW, kAtlasH,
                                       kFirstChar, kNumChars, out->charData.data());
    if (result <= 0) {
        LOGE("stbtt_BakeFontBitmap failed (ret=%d) at pixelHeight=%.1f", result, pixelHeight);
        return false;
    }

    // Font metrics for line height / ascent, scaled to this pixel size.
    stbtt_fontinfo info;
    stbtt_InitFont(&info, fontData, 0);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    out->ascent = ascent * scale;
    out->descent = descent * scale;
    out->lineGap = lineGap * scale;
    out->pixelHeight = pixelHeight;
    out->bitmapW = kAtlasW;
    out->bitmapH = kAtlasH;

    // Expand single-channel coverage into RGBA (white RGB, coverage alpha) so
    // the shared shader (texel * vertex color) works unmodified for text.
    std::vector<unsigned char> rgba(static_cast<size_t>(kAtlasW) * kAtlasH * 4);
    for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[static_cast<size_t>(i)];
    }

    glGenTextures(1, &out->texture);
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasW, kAtlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return out->texture != 0;
}

void FontAtlas::destroy() {
    for (auto& a : atlases_) {
        if (a.texture) {
            glDeleteTextures(1, &a.texture);
            a.texture = 0;
        }
    }
    built_ = false;
}

float FontAtlas::drawText(QuadBatch& batch, const std::string& text, float x, float y,
                           FontSize size, const Color& color) const {
    const SizeAtlas& atlas = atlases_[static_cast<size_t>(size)];
    if (!built_ || atlas.texture == 0) return x;

    float cursorX = x;
    float cursorY = y;
    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < kFirstChar || uc >= kFirstChar + kNumChars) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(atlas.charData.data(), atlas.bitmapW, atlas.bitmapH,
                            uc - kFirstChar, &cursorX, &cursorY, &q, 1);
        Rect r{q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0};
        batch.pushTexturedRect(r, q.s0, q.t0, q.s1, q.t1, atlas.texture, color);
    }
    return cursorX;
}

float FontAtlas::measureText(const std::string& text, FontSize size) const {
    const SizeAtlas& atlas = atlases_[static_cast<size_t>(size)];
    if (!built_ || atlas.texture == 0) return 0.0f;
    float cursorX = 0.0f, cursorY = 0.0f;
    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < kFirstChar || uc >= kFirstChar + kNumChars) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(atlas.charData.data(), atlas.bitmapW, atlas.bitmapH,
                            uc - kFirstChar, &cursorX, &cursorY, &q, 1);
    }
    return cursorX;
}

float FontAtlas::lineHeight(FontSize size) const {
    const SizeAtlas& atlas = atlases_[static_cast<size_t>(size)];
    return atlas.ascent - atlas.descent + atlas.lineGap;
}

float FontAtlas::ascent(FontSize size) const {
    return atlases_[static_cast<size_t>(size)].ascent;
}

} // namespace kiosk::gl
