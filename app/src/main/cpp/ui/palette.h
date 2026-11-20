#pragma once

#include "../gl/quad_batch.h"

// Shared color palette: white/light background, orange-red accent, subtle
// borders, simple typography -- matching the reference kiosk screenshot.
namespace kiosk::ui::palette {

using kiosk::gl::Color;

inline Color accent() { return Color::rgb8(0xE8, 0x44, 0x2A); }       // orange-red
inline Color accentDark() { return Color::rgb8(0xC2, 0x35, 0x1E); }   // pressed/selected
inline Color background() { return Color::rgb8(0xF7, 0xF7, 0xF5); }   // light background
inline Color cardBackground() { return Color::rgb8(0xFF, 0xFF, 0xFF); }
inline Color border() { return Color::rgb8(0xE4, 0xE1, 0xDC); }
inline Color textPrimary() { return Color::rgb8(0x22, 0x20, 0x1E); }
inline Color textSecondary() { return Color::rgb8(0x8A, 0x86, 0x80); }
inline Color textOnAccent() { return Color::rgb8(0xFF, 0xFF, 0xFF); }
inline Color strikethrough() { return Color::rgb8(0xAE, 0xAA, 0xA4); }
inline Color success() { return Color::rgb8(0x2E, 0x9E, 0x5B); }
inline Color placeholderFill() { return Color::rgb8(0xEC, 0xEA, 0xE6); }
inline Color overlayBackdrop() { return Color::rgba(0, 0, 0, 0.55f); }

} // namespace kiosk::ui::palette
