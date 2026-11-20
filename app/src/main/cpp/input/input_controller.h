#pragma once

#include <array>
#include <vector>

#include "../core/product.h"
#include "../layout/layout_engine.h"

namespace kiosk::input {

enum class TouchAction { Down = 0, Up = 1, Move = 2, Cancel = 3 };

enum class ActionType {
    None,
    SelectCategory,
    TapRefresh,
    TapClearCart,
    TapProductAdd,
    TapCartRemove,
    TapCheckout,
    TapOverlayClose,
};

struct AppAction {
    ActionType type = ActionType::None;
    int categoryIndex = -1;
    long productId = -1;
};

// Which scrollable axis a fling (if any) should apply to after release.
enum class FlingTarget { None, GridY, CategoryX, CartX };

struct TouchOutcome {
    AppAction action;
    float gridScrollDelta = 0;
    float categoryScrollDelta = 0;
    float cartScrollDelta = 0;
    bool needsRedraw = false;

    // Set on Up when the gesture released fast enough to fling. Velocity is
    // in px/ms, sign-matched to the corresponding *ScrollDelta convention
    // above (positive = same direction as scroll offset increasing).
    FlingTarget flingTarget = FlingTarget::None;
    float flingVelocity = 0;
};

// Single-pointer tap/drag hit-testing against the current LayoutResult. Only
// the primary pointer is tracked (adequate for a kiosk: tap-to-add,
// drag-to-scroll; no pinch/multi-touch gestures are needed here).
class InputController {
public:
    // `layout` must reflect the layout as of right now (including any scroll
    // offsets already applied from earlier events in this same gesture).
    // `visibleProducts` is parallel to layout.cards (same index -> same
    // product), used only to resolve which product an "Add" tap hit.
    // `timeMs` is the originating MotionEvent's eventTime (ms since boot),
    // used only for velocity estimation; Down/Up/Cancel may pass 0.
    TouchOutcome onTouch(TouchAction action, float x, float y, long timeMs,
                         const layout::LayoutResult& layout,
                         const std::vector<const Product*>& visibleProducts);

private:
    enum class Region { None, Header, CategoryRow, Grid, CartStrip, OverlayClose, OverlayInert };

    struct TouchSample {
        long timeMs = 0;
        float x = 0, y = 0;
    };

    bool down_ = false;
    bool dragging_ = false;
    Region region_ = Region::None;
    float startX_ = 0, startY_ = 0;
    float lastX_ = 0, lastY_ = 0;

    // Fixed-size ring buffer of recent samples, no heap allocation. Release
    // velocity is estimated by a least-squares fit over whatever of these
    // fall within kVelocityWindowMs of the final sample -- the same general
    // approach Android's own VelocityTracker uses -- rather than from only
    // the last two events, so one noisy/zero-dt sample right before release
    // can't dominate (or spuriously trigger) a fling.
    static constexpr size_t kHistorySize = 16;
    std::array<TouchSample, kHistorySize> history_{};
    size_t historyCount_ = 0;
    size_t historyNext_ = 0;

    static constexpr float kTapSlopDp = 8.0f;
    static constexpr long kVelocityWindowMs = 100;
    // Expressed in dp, not raw px, and scaled by density at the comparison
    // site (see onTouch) -- a fixed px/ms threshold would make the same
    // physical finger speed register differently as a fling depending on
    // screen density, which is exactly the kind of density inconsistency
    // this pass is meant to avoid. Below kMinFlingVelocityDpPerSec we treat
    // the release as "just stop" rather than fling; kMaxFlingVelocityDpPerSec
    // guards only against bogus event-time deltas, not real fast flicks.
    static constexpr float kMinFlingVelocityDpPerSec = 200.0f;
    static constexpr float kMaxFlingVelocityDpPerSec = 8000.0f;

    void resetHistory();
    void pushSample(float x, float y, long timeMs);
    // Least-squares velocity (px/ms) of whichever axis the given region
    // scrolls, over the recent window. 0 if there's not enough history.
    float estimateReleaseVelocity(Region region) const;

    Region classifyRegion(float x, float y, const layout::LayoutResult& layout) const;
    AppAction resolveTap(float x, float y, const layout::LayoutResult& layout,
                         const std::vector<const Product*>& visibleProducts) const;
};

} // namespace kiosk::input
