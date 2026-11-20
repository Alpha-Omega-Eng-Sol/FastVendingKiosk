#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../gl/quad_batch.h" // for gl::Rect

namespace kiosk::layout {

using gl::Rect;

struct Point { float x, y; };

struct CardLayout {
    Rect card;
    Rect image;
    Rect title;      // text origin area (top-left anchored, up to 2 lines)
    Rect price;
    Rect addButton;
    Rect badge;       // discount badge, top-left corner of image
};

struct CartChipLayout {
    Rect chip;
    Rect thumbnail;
    Rect removeButton;
    long productId = 0;
};

// Snapshot of every rect/point the renderer and input controller need for one
// frame. Recomputed whenever the screen size, scroll offsets, category
// selection, catalog, or cart contents change -- cheap enough (a few hundred
// products at most) to not bother caching beyond "don't recompute every
// frame if nothing changed", which NativeApp handles via its dirty flag.
struct LayoutResult {
    int screenW = 0, screenH = 0;
    float density = 1.0f;

    Rect header;
    Point titlePos;
    Point subtitlePos;
    Rect refreshButton;
    Rect clearCartButton;

    Rect categoryRow;
    std::vector<Rect> categoryButtons; // parallel to fixedCategories()
    float categoryContentWidth = 0;
    float categoryMaxScrollX = 0;

    Rect grid; // clip region for the scrollable product grid
    std::vector<CardLayout> cards; // parallel to the filtered product list
    int columns = 1;
    float gridContentHeight = 0;
    float gridMaxScrollY = 0;

    bool cartVisible = false;
    Rect cartStrip;
    std::vector<CartChipLayout> cartChips;
    float cartContentWidth = 0;
    float cartMaxScrollX = 0;
    Point cartTotalTextPos;
    Rect checkoutButton;

    bool overlayVisible = false;
    Rect overlayBackdrop;
    Rect overlayCard;
    Rect overlayClose;
    Rect overlayQr;
};

struct LayoutInput {
    int screenW = 0;
    int screenH = 0;
    float density = 1.0f;

    std::vector<std::string> categories; // fixedCategories()
    int selectedCategoryIndex = 0;
    float categoryScrollX = 0;

    int cardCount = 0; // size of the currently-filtered product list
    float gridScrollY = 0;

    bool cartVisible = false;
    std::vector<long> cartProductIds; // one entry per line, in display order
    float cartScrollX = 0;

    bool overlayVisible = false;

    // Measures a label's width at the category-button font size; injected
    // rather than taking a hard dependency on gl::FontAtlas so this module
    // stays decoupled from the rendering backend.
    std::function<float(const std::string&)> measureCategoryLabel;
};

class LayoutEngine {
public:
    // Writes into `out` in place (clearing its vectors rather than
    // constructing a fresh LayoutResult) instead of returning by value. This
    // is called every fling tick (~60/s) against a 100+ product catalog, so
    // avoiding a fresh heap allocation per call matters; the caller is
    // expected to keep `out` as a persistent, reused object (NativeApp does).
    void recompute(const LayoutInput& input, LayoutResult& out) const;

    static constexpr float kMinCardWidthDp = 148.0f;
};

} // namespace kiosk::layout
