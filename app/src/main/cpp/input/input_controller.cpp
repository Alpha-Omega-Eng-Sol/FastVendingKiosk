#include "input_controller.h"

#include <algorithm>
#include <cmath>

namespace kiosk::input {

using layout::LayoutResult;

InputController::Region InputController::classifyRegion(float x, float y, const LayoutResult& layout) const {
    if (layout.overlayVisible) {
        if (layout.overlayClose.contains(x, y)) return Region::OverlayClose;
        return Region::OverlayInert; // card body and backdrop both absorb touches; only Close dismisses
    }
    if (layout.header.contains(x, y)) return Region::Header;
    if (layout.categoryRow.contains(x, y)) return Region::CategoryRow;
    if (layout.cartVisible && layout.cartStrip.contains(x, y)) return Region::CartStrip;
    if (layout.grid.contains(x, y)) return Region::Grid;
    return Region::None;
}

void InputController::resetHistory() {
    historyCount_ = 0;
    historyNext_ = 0;
}

void InputController::pushSample(float x, float y, long timeMs) {
    history_[historyNext_] = TouchSample{timeMs, x, y};
    historyNext_ = (historyNext_ + 1) % kHistorySize;
    if (historyCount_ < kHistorySize) ++historyCount_;
}

float InputController::estimateReleaseVelocity(Region region) const {
    if (historyCount_ < 2) return 0.0f;

    // Walk the ring buffer newest-to-oldest, keeping samples within the
    // trailing kVelocityWindowMs window relative to the newest sample. The
    // most recent write is at (historyNext_ - 1) mod kHistorySize.
    long newestTime = 0;
    bool haveNewest = false;
    double sumT = 0, sumP = 0, sumTT = 0, sumTP = 0;
    int n = 0;

    for (size_t i = 0; i < historyCount_; ++i) {
        size_t idx = (historyNext_ + kHistorySize - 1 - i) % kHistorySize;
        const TouchSample& s = history_[idx];
        if (!haveNewest) {
            newestTime = s.timeMs;
            haveNewest = true;
        }
        long age = newestTime - s.timeMs;
        if (age > kVelocityWindowMs) break; // ring buffer is time-ordered, so we can stop here
        float pos = (region == Region::Grid) ? s.y : s.x;
        double t = (double)(s.timeMs - newestTime); // <= 0, newest sample at t=0
        sumT += t;
        sumP += pos;
        sumTT += t * t;
        sumTP += t * pos;
        ++n;
    }

    if (n < 2) return 0.0f;

    // Least-squares slope of pos(t): same formula VelocityTracker's
    // linear-regression strategy uses, robust to a single noisy sample
    // because it fits the whole recent window instead of just the last two
    // points.
    double denom = (double)n * sumTT - sumT * sumT;
    if (std::fabs(denom) < 1e-6) return 0.0f;
    double slope = ((double)n * sumTP - sumT * sumP) / denom; // px per ms, in position terms (not yet sign-flipped)

    // Positive slope means position increases with time (finger moving in
    // +x/+y direction); our scroll convention is the *opposite* sign (see
    // the Move handler below), so flip here to match.
    return (float)(-slope);
}

AppAction InputController::resolveTap(float x, float y, const LayoutResult& layout,
                                       const std::vector<const Product*>& visibleProducts) const {
    AppAction action;
    switch (region_) {
        case Region::OverlayClose:
            action.type = ActionType::TapOverlayClose;
            return action;
        case Region::OverlayInert:
            return action; // tapping the card body or backdrop does nothing

        case Region::Header:
            if (layout.refreshButton.contains(x, y)) {
                action.type = ActionType::TapRefresh;
            } else if (layout.clearCartButton.contains(x, y)) {
                action.type = ActionType::TapClearCart;
            }
            return action;

        case Region::CategoryRow:
            for (size_t i = 0; i < layout.categoryButtons.size(); ++i) {
                if (layout.categoryButtons[i].contains(x, y)) {
                    action.type = ActionType::SelectCategory;
                    action.categoryIndex = (int)i;
                    break;
                }
            }
            return action;

        case Region::Grid:
            for (size_t i = 0; i < layout.cards.size() && i < visibleProducts.size(); ++i) {
                if (layout.cards[i].addButton.contains(x, y)) {
                    action.type = ActionType::TapProductAdd;
                    action.productId = visibleProducts[i] ? visibleProducts[i]->id : -1;
                    break;
                }
            }
            return action;

        case Region::CartStrip:
            for (const auto& chip : layout.cartChips) {
                if (chip.removeButton.contains(x, y)) {
                    action.type = ActionType::TapCartRemove;
                    action.productId = chip.productId;
                    return action;
                }
            }
            if (layout.checkoutButton.contains(x, y)) {
                action.type = ActionType::TapCheckout;
            }
            return action;

        default:
            return action;
    }
}

TouchOutcome InputController::onTouch(TouchAction action, float x, float y, long timeMs,
                                       const LayoutResult& layout,
                                       const std::vector<const Product*>& visibleProducts) {
    TouchOutcome outcome;
    float slop = kTapSlopDp * layout.density;

    switch (action) {
        case TouchAction::Down: {
            down_ = true;
            dragging_ = false;
            startX_ = lastX_ = x;
            startY_ = lastY_ = y;
            resetHistory();
            pushSample(x, y, timeMs);
            region_ = classifyRegion(x, y, layout);
            break;
        }
        case TouchAction::Move: {
            if (!down_) break;
            float dx = x - lastX_;
            float dy = y - lastY_;
            float totalDist = std::sqrt((x - startX_) * (x - startX_) + (y - startY_) * (y - startY_));
            if (!dragging_ && totalDist > slop) dragging_ = true;

            if (dragging_) {
                switch (region_) {
                    case Region::Grid:
                        outcome.gridScrollDelta = -dy; // drag finger up -> scroll content up
                        outcome.needsRedraw = true;
                        break;
                    case Region::CategoryRow:
                        outcome.categoryScrollDelta = -dx;
                        outcome.needsRedraw = true;
                        break;
                    case Region::CartStrip:
                        outcome.cartScrollDelta = -dx;
                        outcome.needsRedraw = true;
                        break;
                    default:
                        break;
                }
            }
            // Recorded regardless of `dragging_` (pre-slop samples still
            // belong to this gesture's recent history), but duplicate
            // (x, y, timeMs) events -- some devices resend the same sample
            // with the same timestamp -- aren't useful for a slope fit, so
            // skip those rather than polluting the window.
            if (timeMs != history_[(historyNext_ + kHistorySize - 1) % kHistorySize].timeMs || historyCount_ == 0) {
                pushSample(x, y, timeMs);
            }
            lastX_ = x;
            lastY_ = y;
            break;
        }
        case TouchAction::Up: {
            if (down_ && !dragging_) {
                outcome.action = resolveTap(x, y, layout, visibleProducts);
                if (outcome.action.type != ActionType::None) outcome.needsRedraw = true;
            } else if (down_ && dragging_) {
                pushSample(x, y, timeMs);
                float v = estimateReleaseVelocity(region_);
                float minPxPerMs = (kMinFlingVelocityDpPerSec / 1000.0f) * layout.density;
                float maxPxPerMs = (kMaxFlingVelocityDpPerSec / 1000.0f) * layout.density;
                if (std::fabs(v) >= minPxPerMs) {
                    v = std::clamp(v, -maxPxPerMs, maxPxPerMs);
                    switch (region_) {
                        case Region::Grid: outcome.flingTarget = FlingTarget::GridY; break;
                        case Region::CategoryRow: outcome.flingTarget = FlingTarget::CategoryX; break;
                        case Region::CartStrip: outcome.flingTarget = FlingTarget::CartX; break;
                        default: break;
                    }
                    if (outcome.flingTarget != FlingTarget::None) outcome.flingVelocity = v;
                }
            }
            down_ = false;
            dragging_ = false;
            region_ = Region::None;
            break;
        }
        case TouchAction::Cancel: {
            down_ = false;
            dragging_ = false;
            region_ = Region::None;
            break;
        }
    }
    return outcome;
}

} // namespace kiosk::input
