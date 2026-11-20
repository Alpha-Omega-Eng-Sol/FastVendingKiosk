#include "checkout_overlay.h"

#include "palette.h"

namespace kiosk::ui {

using gl::Color;
using gl::Rect;

void CheckoutOverlay::draw(gl::QuadBatch& batch, const gl::FontAtlas& font, const layout::LayoutResult& layout,
                            int itemCount, const std::string& totalFormatted) const {
    if (!layout.overlayVisible) return;

    batch.pushSolidRect(layout.overlayBackdrop, palette::overlayBackdrop());
    batch.pushSolidRect(layout.overlayCard, palette::cardBackground());

    const Rect& card = layout.overlayCard;
    float cx = card.x + card.w / 2;

    // Title + subtitle, centered.
    {
        std::string title = "Checkout";
        float w = font.measureText(title, gl::FontSize::Large);
        font.drawText(batch, title, cx - w / 2, card.y + 28 * layout.density + font.ascent(gl::FontSize::Large),
                      gl::FontSize::Large, palette::textPrimary());
    }
    {
        std::string subtitle = "Payment QR - Test Mode";
        float w = font.measureText(subtitle, gl::FontSize::Small);
        font.drawText(batch, subtitle, cx - w / 2,
                      card.y + 48 * layout.density + font.ascent(gl::FontSize::Small),
                      gl::FontSize::Small, palette::textSecondary());
    }

    // Deterministic, explicitly-fake QR-like placeholder: a fixed-seed grid
    // of black/white blocks inside a bordered square. Never a real QR
    // encoding -- this is a test-mode visual only.
    {
        const Rect& qr = layout.overlayQr;
        batch.pushSolidRect(Rect{qr.x - 2, qr.y - 2, qr.w + 4, qr.h + 4}, palette::border());
        batch.pushSolidRect(qr, Color::rgb8(255, 255, 255));

        const int kGrid = 9;
        float cell = qr.w / kGrid;
        uint32_t seed = 0x9E3779B9u ^ (uint32_t)itemCount;
        for (int row = 0; row < kGrid; ++row) {
            for (int col = 0; col < kGrid; ++col) {
                // Finder-pattern-style hollow ring corners, like a real QR,
                // purely for visual flavor (still not decodable / not
                // claimed to be).
                bool isCorner = (row < 3 && col < 3) || (row < 3 && col >= kGrid - 3) ||
                                (row >= kGrid - 3 && col < 3);
                bool filled;
                if (isCorner) {
                    int rr = row < 3 ? row : row - (kGrid - 3);
                    int cc = col < 3 ? col : col - (kGrid - 3);
                    filled = (rr == 0 || rr == 2 || cc == 0 || cc == 2); // ring only, hollow center
                } else {
                    seed = seed * 1664525u + 1013904223u;
                    filled = ((seed >> 24) & 1) != 0;
                }
                if (filled) {
                    batch.pushSolidRect(Rect{qr.x + col * cell, qr.y + row * cell, cell, cell},
                                        Color::rgb8(0x1A, 0x1A, 0x1A));
                }
            }
        }
    }

    float textY = layout.overlayQr.y + layout.overlayQr.h + 28 * layout.density;
    {
        std::string hello = "Hello, checkout testing.";
        float w = font.measureText(hello, gl::FontSize::Regular);
        font.drawText(batch, hello, cx - w / 2, textY + font.ascent(gl::FontSize::Regular),
                      gl::FontSize::Regular, palette::textPrimary());
    }
    textY += 26 * layout.density;
    {
        std::string summary = std::to_string(itemCount) + (itemCount == 1 ? " item - " : " items - ") + totalFormatted;
        float w = font.measureText(summary, gl::FontSize::Regular);
        font.drawText(batch, summary, cx - w / 2, textY + font.ascent(gl::FontSize::Regular),
                      gl::FontSize::Regular, palette::accent());
    }

    // Close button (top-right of the card).
    batch.pushSolidRect(layout.overlayClose, palette::border());
    {
        std::string x = "X";
        float w = font.measureText(x, gl::FontSize::Regular);
        float h = font.lineHeight(gl::FontSize::Regular);
        font.drawText(batch, x, layout.overlayClose.x + (layout.overlayClose.w - w) / 2,
                      layout.overlayClose.y + (layout.overlayClose.h + h) / 2 - font.lineHeight(gl::FontSize::Regular) * 0.15f,
                      gl::FontSize::Regular, palette::textPrimary());
    }
}

} // namespace kiosk::ui
