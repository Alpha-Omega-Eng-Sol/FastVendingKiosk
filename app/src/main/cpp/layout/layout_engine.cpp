#include "layout_engine.h"

#include <algorithm>
#include <cmath>

namespace kiosk::layout {

namespace {

constexpr float kHeaderHeightDp = 64.0f;
constexpr float kCategoryRowHeightDp = 48.0f;
constexpr float kCartStripHeightDp = 84.0f;
constexpr float kGridPaddingDp = 10.0f;
constexpr float kCardImageAspect = 0.62f; // image height = cardWidth * aspect
constexpr float kCardTitleHeightDp = 34.0f;
constexpr float kCardPriceHeightDp = 22.0f;
constexpr float kCardButtonHeightDp = 30.0f;
constexpr float kCardInnerPaddingDp = 6.0f;
constexpr float kCategoryButtonHPaddingDp = 16.0f;
constexpr float kCategoryButtonHeightDp = 32.0f;
constexpr float kCategoryGapDp = 8.0f;
constexpr float kCartChipSizeDp = 56.0f;
constexpr float kCartChipGapDp = 8.0f;

} // namespace

void LayoutEngine::recompute(const LayoutInput& in, LayoutResult& out) const {
    out.categoryButtons.clear();
    out.cards.clear();
    out.cartChips.clear();

    out.screenW = in.screenW;
    out.screenH = in.screenH;
    out.density = in.density <= 0 ? 1.0f : in.density;
    auto px = [&](float dp) { return dp * out.density; };

    float y = 0;

    // --- Header ---
    float headerH = px(kHeaderHeightDp);
    out.header = Rect{0, 0, (float)in.screenW, headerH};
    out.titlePos = Point{px(16), px(24)};
    out.subtitlePos = Point{px(16), px(44)};
    float btnH = px(32), btnW = px(84);
    out.clearCartButton = Rect{(float)in.screenW - btnW - px(12), (headerH - btnH) / 2, btnW, btnH};
    out.refreshButton = Rect{out.clearCartButton.x - btnW - px(8), (headerH - btnH) / 2, btnW, btnH};
    y += headerH;

    // --- Category row ---
    float catRowH = px(kCategoryRowHeightDp);
    out.categoryRow = Rect{0, y, (float)in.screenW, catRowH};
    {
        float cx = px(12) - in.categoryScrollX;
        float btnHgt = px(kCategoryButtonHeightDp);
        float btnY = y + (catRowH - btnHgt) / 2;
        float gap = px(kCategoryGapDp);
        out.categoryButtons.reserve(in.categories.size());
        for (const auto& label : in.categories) {
            float textW = in.measureCategoryLabel ? in.measureCategoryLabel(label) : (float)label.size() * px(7.0f);
            float w = textW + px(kCategoryButtonHPaddingDp) * 2;
            out.categoryButtons.push_back(Rect{cx, btnY, w, btnHgt});
            cx += w + gap;
        }
        float contentEnd = cx - gap + px(12) + in.categoryScrollX;
        out.categoryContentWidth = std::max(contentEnd, (float)in.screenW);
        out.categoryMaxScrollX = std::max(0.0f, out.categoryContentWidth - (float)in.screenW);
    }
    y += catRowH;

    // --- Cart strip (reserve space if visible) ---
    float cartH = in.cartVisible ? px(kCartStripHeightDp) : 0.0f;

    // --- Grid occupies everything between category row and cart strip ---
    float gridTop = y;
    float gridBottom = (float)in.screenH - cartH;
    out.grid = Rect{0, gridTop, (float)in.screenW, std::max(0.0f, gridBottom - gridTop)};

    {
        float padding = px(kGridPaddingDp);
        float minCardW = px(kMinCardWidthDp);
        float contentW = std::max(1.0f, out.grid.w - 2 * padding);
        int columns = std::max(1, (int)std::floor((contentW + padding) / (minCardW + padding)));
        out.columns = columns;
        float cardW = (contentW - (columns - 1) * padding) / columns;
        float imageH = cardW * kCardImageAspect;
        float cardH = imageH + px(kCardTitleHeightDp) + px(kCardPriceHeightDp) + px(kCardButtonHeightDp) +
                      px(kCardInnerPaddingDp) * 4;

        int rows = (in.cardCount + columns - 1) / columns;
        out.gridContentHeight = padding + rows * (cardH + padding);
        out.gridMaxScrollY = std::max(0.0f, out.gridContentHeight - out.grid.h);

        out.cards.reserve((size_t)in.cardCount);
        for (int i = 0; i < in.cardCount; ++i) {
            int row = i / columns;
            int col = i % columns;
            float cardX = out.grid.x + padding + col * (cardW + padding);
            float cardY = out.grid.y + padding + row * (cardH + padding) - in.gridScrollY;

            CardLayout cl;
            cl.card = Rect{cardX, cardY, cardW, cardH};
            float inner = px(kCardInnerPaddingDp);
            cl.image = Rect{cardX, cardY, cardW, imageH};
            cl.badge = Rect{cardX + inner, cardY + inner, px(34), px(18)};
            float textY = cardY + imageH + inner;
            cl.title = Rect{cardX + inner, textY, cardW - 2 * inner, px(kCardTitleHeightDp)};
            textY += px(kCardTitleHeightDp);
            cl.price = Rect{cardX + inner, textY, cardW - 2 * inner, px(kCardPriceHeightDp)};
            textY += px(kCardPriceHeightDp) + inner;
            cl.addButton = Rect{cardX + inner, textY, cardW - 2 * inner, px(kCardButtonHeightDp)};
            out.cards.push_back(cl);
        }
    }

    // --- Cart strip ---
    out.cartVisible = in.cartVisible;
    if (in.cartVisible) {
        out.cartStrip = Rect{0, (float)in.screenH - cartH, (float)in.screenW, cartH};
        float chipSize = px(kCartChipSizeDp);
        float gap = px(kCartChipGapDp);
        float chipY = out.cartStrip.y + (cartH - chipSize) / 2;
        float checkoutW = px(120), checkoutH = px(44);
        out.checkoutButton = Rect{(float)in.screenW - checkoutW - px(12),
                                   out.cartStrip.y + (cartH - checkoutH) / 2, checkoutW, checkoutH};
        float totalsReserved = px(150); // space for item-count/total text before the checkout button
        float chipsRightEdge = out.checkoutButton.x - totalsReserved;

        float cx = px(12) - in.cartScrollX;
        out.cartChips.reserve(in.cartProductIds.size());
        for (long id : in.cartProductIds) {
            CartChipLayout chip;
            chip.productId = id;
            chip.chip = Rect{cx, chipY, chipSize, chipSize};
            chip.thumbnail = Rect{cx, chipY, chipSize, chipSize};
            float removeSize = px(18);
            chip.removeButton = Rect{cx + chipSize - removeSize, chipY - removeSize / 2, removeSize, removeSize};
            out.cartChips.push_back(chip);
            cx += chipSize + gap;
        }
        float contentEnd = cx - gap + px(12) + in.cartScrollX;
        out.cartContentWidth = std::max(contentEnd, chipsRightEdge);
        out.cartMaxScrollX = std::max(0.0f, out.cartContentWidth - (chipsRightEdge - px(12)));
        out.cartTotalTextPos = Point{chipsRightEdge + px(8), out.cartStrip.y + cartH / 2 - px(14)};
    }

    // --- Checkout overlay ---
    out.overlayVisible = in.overlayVisible;
    if (in.overlayVisible) {
        out.overlayBackdrop = Rect{0, 0, (float)in.screenW, (float)in.screenH};
        float cardW = std::min((float)in.screenW - px(48), px(360));
        float cardH = std::min((float)in.screenH - px(48), px(420));
        float cx = (in.screenW - cardW) / 2;
        float cy = (in.screenH - cardH) / 2;
        out.overlayCard = Rect{cx, cy, cardW, cardH};
        float closeSize = px(32);
        out.overlayClose = Rect{cx + cardW - closeSize - px(10), cy + px(10), closeSize, closeSize};
        float qrSize = std::min(cardW - px(64), px(180));
        out.overlayQr = Rect{cx + (cardW - qrSize) / 2, cy + px(84), qrSize, qrSize};
    }
}

} // namespace kiosk::layout
