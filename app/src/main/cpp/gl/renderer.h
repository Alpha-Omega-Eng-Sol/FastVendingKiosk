#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>
#include <string>
#include <vector>

#include "../core/cart_manager.h"
#include "../core/product_repository.h"
#include "../image/image_manager.h"
#include "../layout/layout_engine.h"
#include "../ui/checkout_overlay.h"
#include "font_atlas.h"
#include "quad_batch.h"

namespace kiosk {

enum class CatalogLoadState { Loading, Loaded, Error };

// Everything the renderer needs to draw exactly one frame. Built fresh by
// NativeApp each time a frame is needed; the renderer itself holds no
// business state beyond its GL resources.
struct FrameState {
    const layout::LayoutResult* layout = nullptr;
    const ProductRepository* repository = nullptr;
    const CartManager* cart = nullptr;
    ImageManager* images = nullptr; // non-const: lazily creates textures on draw
    const std::vector<const Product*>* visibleProducts = nullptr; // parallel to layout->cards
    int selectedCategoryIndex = 0;
    CatalogLoadState catalogState = CatalogLoadState::Loading;
    std::string errorMessage;
    bool diagnosticsEnabled = false;
};

// Owns EGL/GLES state: display, config, context, window surface. Lives
// entirely on the dedicated render thread (see NativeApp). On any surface
// loss, destroyContext() tears everything down including GL textures/shaders
// (nothing is assumed to survive); the next createContext() rebuilds from
// scratch, including re-baking the font atlas.
class Renderer {
public:
    bool createContext(ANativeWindow* window, AAssetManager* assetManager, float density);
    void destroyContext();
    void resize(int width, int height);
    bool isReady() const { return eglSurface_ != EGL_NO_SURFACE && eglContext_ != EGL_NO_CONTEXT; }

    int width() const { return width_; }
    int height() const { return height_; }

    void draw(const FrameState& state);

    // Exposed so LayoutEngine can size category buttons to their label text.
    // Only meaningful once createContext() has succeeded (font baked).
    float measureText(const std::string& text, gl::FontSize size) const { return font_.measureText(text, size); }

private:
    void drawHeader(const layout::LayoutResult& layout, bool cartHasItems);
    void drawCategoryRow(const layout::LayoutResult& layout, const std::vector<std::string>& categories,
                         int selectedIndex);
    void drawGrid(const FrameState& state);
    void drawCard(const layout::CardLayout& card, const Product& product, int quantityInCart);
    void drawCartStrip(const FrameState& state);
    void drawLoadingOrError(const layout::LayoutResult& layout, const FrameState& state);

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLConfig eglConfig_ = nullptr;
    ANativeWindow* window_ = nullptr; // owns one ANativeWindow_acquire-equivalent ref; released in destroyContext()

    int width_ = 0, height_ = 0;

    gl::QuadBatch batch_;
    gl::FontAtlas font_;
    ui::CheckoutOverlay overlay_;
};

} // namespace kiosk
