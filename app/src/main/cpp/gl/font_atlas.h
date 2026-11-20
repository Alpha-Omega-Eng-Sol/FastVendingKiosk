#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>
#include <array>
#include <string>
#include <vector>

#include "quad_batch.h"

// stb_truetype is a single-header public-domain library; the implementation
// is compiled once in vendor/stb_impl.cpp.
#include "../vendor/stb_truetype.h"

namespace kiosk::gl {

enum class FontSize { Small, Regular, Large, Header, Count };

// Bakes a bitmap glyph atlas per size (stb_truetype's simple
// stbtt_BakeFontBitmap path -- good enough for a fixed, known ASCII range at
// a handful of fixed pixel sizes; no need for the more complex rect-packing
// API since sizes are small and known ahead of time). One GL texture per
// size, built once at startup/context-recreation and reused every frame --
// we never rasterize text at draw time.
class FontAtlas {
public:
    ~FontAtlas() { destroy(); }

    // Loads the bundled Roboto TTF via the Android asset manager and bakes
    // all FontSize atlases at pixel sizes scaled by `density`. Must be
    // called on the GL thread (creates textures).
    bool build(AAssetManager* assetManager, float density);
    void destroy();

    // Appends quads for `text` to `batch` with baseline at (x, y); returns
    // the advanced x position after the string (useful for manual layout).
    float drawText(QuadBatch& batch, const std::string& text, float x, float y,
                    FontSize size, const Color& color) const;

    float measureText(const std::string& text, FontSize size) const;
    float lineHeight(FontSize size) const;
    float ascent(FontSize size) const;

private:
    struct SizeAtlas {
        GLuint texture = 0;
        int bitmapW = 0, bitmapH = 0;
        float pixelHeight = 0;
        std::array<stbtt_bakedchar, 96> charData{}; // ASCII 32..127
        float ascent = 0, descent = 0, lineGap = 0;
    };

    bool bakeSize(const unsigned char* fontData, float pixelHeight, SizeAtlas* out);

    std::array<SizeAtlas, static_cast<size_t>(FontSize::Count)> atlases_;
    std::vector<unsigned char> fontBuffer_;
    bool built_ = false;
};

} // namespace kiosk::gl
