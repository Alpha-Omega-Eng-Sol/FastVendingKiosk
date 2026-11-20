#pragma once

#include <string>

#include "../gl/font_atlas.h"
#include "../gl/quad_batch.h"
#include "../layout/layout_engine.h"

namespace kiosk::ui {

// Draws the "Checkout" test overlay: title, subtitle, a high-contrast
// QR-*like* placeholder (an explicitly fake, deterministic block pattern --
// never claimed to be scannable), item count, discounted total, and a Close
// button. Pure draw function; CheckoutOverlay has no state of its own, the
// visible/hidden flag lives in NativeApp alongside the rest of the UI state.
class CheckoutOverlay {
public:
    void draw(gl::QuadBatch& batch, const gl::FontAtlas& font, const layout::LayoutResult& layout,
              int itemCount, const std::string& totalFormatted) const;
};

} // namespace kiosk::ui
