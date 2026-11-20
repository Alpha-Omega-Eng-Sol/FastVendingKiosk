#include "renderer.h"

#include <algorithm>
#include <cstdio>

#include "../core/product_repository.h"
#include "../ui/palette.h"
#include "gl_common.h"

namespace kiosk {

using gl::Color;
using gl::Rect;
using namespace ui::palette;

namespace {

std::string truncateToFit(const gl::FontAtlas& font, const std::string& text, gl::FontSize size, float maxWidth) {
    if (font.measureText(text, size) <= maxWidth) return text;
    std::string ellipsis = "...";
    float ellipsisW = font.measureText(ellipsis, size);
    std::string result;
    for (char c : text) {
        std::string candidate = result + c;
        if (font.measureText(candidate, size) + ellipsisW > maxWidth) break;
        result = candidate;
    }
    return result + ellipsis;
}

} // namespace

bool Renderer::createContext(ANativeWindow* window, AAssetManager* assetManager, float density) {
    destroyContext();
    window_ = window; // ownership transferred from caller; released in destroyContext()

    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    if (!eglInitialize(eglDisplay_, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }

    // GLES 2.0-compatible config: RGB888 + depth not needed (2D UI), stencil
    // not needed. Keep it broadly supported rather than requesting optional
    // extensions up front.
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        destroyContext();
        return false;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); destroyContext(); return false; }

    eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_, window, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); destroyContext(); return false; }

    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        LOGE("eglMakeCurrent failed");
        destroyContext();
        return false;
    }

    eglQuerySurface(eglDisplay_, eglSurface_, EGL_WIDTH, &width_);
    eglQuerySurface(eglDisplay_, eglSurface_, EGL_HEIGHT, &height_);

    if (!batch_.init()) { LOGE("QuadBatch init failed"); destroyContext(); return false; }
    if (!font_.build(assetManager, density)) { LOGE("FontAtlas build failed"); destroyContext(); return false; }

    LOGI("EGL context created: %dx%d", width_, height_);
    return true;
}

void Renderer::destroyContext() {
    batch_.destroy();
    font_.destroy();

    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
        if (eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglSurface_);
        eglTerminate(eglDisplay_);
    }
    eglDisplay_ = EGL_NO_DISPLAY;
    eglSurface_ = EGL_NO_SURFACE;
    eglContext_ = EGL_NO_CONTEXT;
    eglConfig_ = nullptr;
    width_ = height_ = 0;

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void Renderer::draw(const FrameState& state) {
    if (!isReady() || !state.layout) return;
    const layout::LayoutResult& layout = *state.layout;

    glViewport(0, 0, width_, height_);
    Color bg = background();
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    batch_.begin(width_, height_);

    drawHeader(layout, state.cart && !state.cart->isEmpty());
    drawCategoryRow(layout, kiosk::fixedCategories(), state.selectedCategoryIndex);

    if (state.catalogState != CatalogLoadState::Loaded || (state.repository && state.repository->isEmpty())) {
        drawLoadingOrError(layout, state);
    } else {
        drawGrid(state);
    }

    if (layout.cartVisible) drawCartStrip(state);

    if (layout.overlayVisible && state.cart) {
        overlay_.draw(batch_, font_, layout, state.cart->totalItemCount(),
                      state.repository ? money::formatUsd(state.cart->totalDiscountedCents(*state.repository)) : "$0.00");
    }

    batch_.end();
    eglSwapBuffers(eglDisplay_, eglSurface_);
}

void Renderer::drawHeader(const layout::LayoutResult& layout, bool cartHasItems) {
    batch_.pushSolidRect(layout.header, accent());

    font_.drawText(batch_, "Vending King", layout.titlePos.x,
                   layout.titlePos.y + font_.ascent(gl::FontSize::Header), gl::FontSize::Header, textOnAccent());
    font_.drawText(batch_, "Fast Vending Kiosk", layout.subtitlePos.x,
                   layout.subtitlePos.y + font_.ascent(gl::FontSize::Small), gl::FontSize::Small,
                   Color::rgba(1, 1, 1, 0.85f));

    // Refresh button
    batch_.pushSolidRect(layout.refreshButton, Color::rgba(1, 1, 1, 0.18f));
    {
        std::string label = "Refresh";
        float w = font_.measureText(label, gl::FontSize::Small);
        font_.drawText(batch_, label, layout.refreshButton.x + (layout.refreshButton.w - w) / 2,
                       layout.refreshButton.y + (layout.refreshButton.h + font_.ascent(gl::FontSize::Small)) / 2 - 2,
                       gl::FontSize::Small, textOnAccent());
    }

    // Clear Cart button
    batch_.pushSolidRect(layout.clearCartButton, cartHasItems ? Color::rgba(1, 1, 1, 0.22f) : Color::rgba(1, 1, 1, 0.08f));
    {
        std::string label = "Clear Cart";
        float w = font_.measureText(label, gl::FontSize::Small);
        Color textCol = cartHasItems ? textOnAccent() : Color::rgba(1, 1, 1, 0.5f);
        font_.drawText(batch_, label, layout.clearCartButton.x + (layout.clearCartButton.w - w) / 2,
                       layout.clearCartButton.y + (layout.clearCartButton.h + font_.ascent(gl::FontSize::Small)) / 2 - 2,
                       gl::FontSize::Small, textCol);
    }
}

void Renderer::drawCategoryRow(const layout::LayoutResult& layout, const std::vector<std::string>& categories,
                               int selectedIndex) {
    batch_.pushSolidRect(layout.categoryRow, cardBackground());
    batch_.pushSolidRect(Rect{0, layout.categoryRow.y + layout.categoryRow.h - 1, layout.categoryRow.w, 1}, border());

    batch_.pushClip(layout.categoryRow);
    for (size_t i = 0; i < layout.categoryButtons.size() && i < categories.size(); ++i) {
        const Rect& r = layout.categoryButtons[i];
        bool selected = (int)i == selectedIndex;
        batch_.pushSolidRect(r, selected ? accent() : placeholderFill());
        float textW = font_.measureText(categories[i], gl::FontSize::Regular);
        font_.drawText(batch_, categories[i], r.x + (r.w - textW) / 2,
                       r.y + (r.h + font_.ascent(gl::FontSize::Regular)) / 2 - 3, gl::FontSize::Regular,
                       selected ? textOnAccent() : textPrimary());
    }
    batch_.popClip();
}

void Renderer::drawCard(const layout::CardLayout& card, const Product& product, int quantityInCart) {
    batch_.pushSolidRect(card.card, cardBackground());
    batch_.pushSolidRect(Rect{card.card.x, card.card.y, card.card.w, 1}, border());
    batch_.pushSolidRect(Rect{card.card.x, card.card.y, 1, card.card.h}, border());
    batch_.pushSolidRect(Rect{card.card.x + card.card.w - 1, card.card.y, 1, card.card.h}, border());
    batch_.pushSolidRect(Rect{card.card.x, card.card.y + card.card.h - 1, card.card.w, 1}, border());

    std::string title = truncateToFit(font_, product.title, gl::FontSize::Regular, card.title.w);
    font_.drawText(batch_, title, card.title.x, card.title.y + font_.ascent(gl::FontSize::Regular),
                   gl::FontSize::Regular, textPrimary());

    std::string discounted = money::formatUsd(product.discountedPriceCents());
    float priceX = card.price.x;
    priceX = font_.drawText(batch_, discounted, priceX, card.price.y + font_.ascent(gl::FontSize::Regular),
                            gl::FontSize::Regular, accent());
    if (product.hasDiscount()) {
        std::string original = money::formatUsd(product.priceCents());
        float origX = priceX + 6;
        float origW = font_.measureText(original, gl::FontSize::Small);
        font_.drawText(batch_, original, origX, card.price.y + font_.ascent(gl::FontSize::Small) + 2,
                       gl::FontSize::Small, strikethrough());
        float strikeY = card.price.y + font_.ascent(gl::FontSize::Small) + 2 - font_.ascent(gl::FontSize::Small) * 0.35f;
        batch_.pushSolidRect(Rect{origX, strikeY, origW, 1}, strikethrough());

        // Only show the "-N%" badge once it rounds to at least 1% -- a
        // "-0%" badge for a sub-0.5% discount would be confusing even
        // though the discounted price itself is still correct above.
        int discountPct = (int)(product.discountPercentage + 0.5);
        if (discountPct >= 1) {
            std::string badge = "-" + std::to_string(discountPct) + "%";
            batch_.pushSolidRect(card.badge, accent());
            float bw = font_.measureText(badge, gl::FontSize::Small);
            font_.drawText(batch_, badge, card.badge.x + (card.badge.w - bw) / 2,
                           card.badge.y + (card.badge.h + font_.ascent(gl::FontSize::Small)) / 2 - 3,
                           gl::FontSize::Small, textOnAccent());
        }
    }

    std::string btnLabel = quantityInCart > 0 ? ("Add (" + std::to_string(quantityInCart) + ")") : "Add";
    batch_.pushSolidRect(card.addButton, accent());
    float btnTextW = font_.measureText(btnLabel, gl::FontSize::Small);
    font_.drawText(batch_, btnLabel, card.addButton.x + (card.addButton.w - btnTextW) / 2,
                   card.addButton.y + (card.addButton.h + font_.ascent(gl::FontSize::Small)) / 2 - 3,
                   gl::FontSize::Small, textOnAccent());
}

void Renderer::drawGrid(const FrameState& state) {
    const layout::LayoutResult& layout = *state.layout;
    if (!state.visibleProducts) return;

    batch_.pushClip(layout.grid);
    for (size_t i = 0; i < layout.cards.size() && i < state.visibleProducts->size(); ++i) {
        const layout::CardLayout& card = layout.cards[i];
        // Skip work entirely for cards that don't intersect the visible
        // viewport -- this is the "only draw what's on screen" viewport cull.
        if (card.card.y + card.card.h < layout.grid.y || card.card.y > layout.grid.y + layout.grid.h) {
            continue;
        }
        const Product* product = (*state.visibleProducts)[i];
        if (!product) continue;

        drawCard(card, *product, state.cart ? state.cart->quantityOf(product->id) : 0);

        if (state.images) {
            GLuint tex = state.images->textureFor(product->id);
            if (tex != 0) {
                batch_.pushTexturedRect(card.image, 0, 0, 1, 1, tex);
            } else {
                batch_.pushSolidRect(card.image, placeholderFill());
            }
        } else {
            batch_.pushSolidRect(card.image, placeholderFill());
        }
    }
    batch_.popClip();
}

void Renderer::drawCartStrip(const FrameState& state) {
    const layout::LayoutResult& layout = *state.layout;
    batch_.pushSolidRect(layout.cartStrip, cardBackground());
    batch_.pushSolidRect(Rect{0, layout.cartStrip.y, layout.cartStrip.w, 1}, border());

    float chipsRight = layout.checkoutButton.x - 4;
    Rect chipClip{0, layout.cartStrip.y, chipsRight, layout.cartStrip.h};
    batch_.pushClip(chipClip);
    for (size_t i = 0; i < layout.cartChips.size(); ++i) {
        const layout::CartChipLayout& chip = layout.cartChips[i];
        if (chip.chip.x + chip.chip.w < 0 || chip.chip.x > chipsRight) continue;

        GLuint tex = state.images ? state.images->textureFor(chip.productId) : 0;
        if (tex != 0) {
            batch_.pushTexturedRect(chip.thumbnail, 0, 0, 1, 1, tex);
        } else {
            batch_.pushSolidRect(chip.thumbnail, placeholderFill());
        }
        int qty = state.cart ? state.cart->quantityOf(chip.productId) : 0;
        if (qty > 1) {
            Rect badge{chip.chip.x + chip.chip.w - 18, chip.chip.y - 6, 18, 18};
            batch_.pushSolidRect(badge, accent());
            std::string qtyStr = std::to_string(qty);
            float qw = font_.measureText(qtyStr, gl::FontSize::Small);
            font_.drawText(batch_, qtyStr, badge.x + (badge.w - qw) / 2,
                           badge.y + (badge.h + font_.ascent(gl::FontSize::Small)) / 2 - 3, gl::FontSize::Small,
                           textOnAccent());
        }
        batch_.pushSolidRect(chip.removeButton, Color::rgb8(0x33, 0x33, 0x33));
        float xw = font_.measureText("x", gl::FontSize::Small);
        font_.drawText(batch_, "x", chip.removeButton.x + (chip.removeButton.w - xw) / 2,
                       chip.removeButton.y + (chip.removeButton.h + font_.ascent(gl::FontSize::Small)) / 2 - 3,
                       gl::FontSize::Small, textOnAccent());
    }
    batch_.popClip();

    if (state.cart && state.repository) {
        std::string itemsLine = std::to_string(state.cart->totalItemCount()) + " items";
        std::string totalLine = money::formatUsd(state.cart->totalDiscountedCents(*state.repository));
        font_.drawText(batch_, itemsLine, layout.cartTotalTextPos.x,
                       layout.cartTotalTextPos.y + font_.ascent(gl::FontSize::Small), gl::FontSize::Small,
                       textSecondary());
        font_.drawText(batch_, totalLine, layout.cartTotalTextPos.x,
                       layout.cartTotalTextPos.y + 18 + font_.ascent(gl::FontSize::Regular), gl::FontSize::Regular,
                       textPrimary());
    }

    batch_.pushSolidRect(layout.checkoutButton, accent());
    std::string label = "Checkout";
    float lw = font_.measureText(label, gl::FontSize::Regular);
    font_.drawText(batch_, label, layout.checkoutButton.x + (layout.checkoutButton.w - lw) / 2,
                   layout.checkoutButton.y + (layout.checkoutButton.h + font_.ascent(gl::FontSize::Regular)) / 2 - 3,
                   gl::FontSize::Regular, textOnAccent());
}

void Renderer::drawLoadingOrError(const layout::LayoutResult& layout, const FrameState& state) {
    std::string message;
    Color color = textSecondary();
    if (state.catalogState == CatalogLoadState::Loading) {
        message = "Loading products...";
    } else if (state.catalogState == CatalogLoadState::Error) {
        message = state.errorMessage.empty() ? "Failed to load products. Tap Refresh to retry." : state.errorMessage;
        color = accent();
    } else {
        message = "No products available.";
    }
    float w = font_.measureText(message, gl::FontSize::Large);
    float x = layout.grid.x + (layout.grid.w - w) / 2;
    float y = layout.grid.y + layout.grid.h / 2;
    font_.drawText(batch_, message, std::max(x, layout.grid.x + 16), y, gl::FontSize::Large, color);
}

} // namespace kiosk
