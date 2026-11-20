#include "native_app.h"

#include <algorithm>
#include <cmath>

#include "gl/gl_common.h"

namespace kiosk {

NativeApp& NativeApp::instance() {
    static NativeApp app;
    return app;
}

NativeApp::~NativeApp() {
    postDestroy();
    if (renderThread_.joinable()) renderThread_.join();
}

void NativeApp::init(JavaVM* vm, AAssetManager* assetManager, float density) {
    vm_ = vm;
    assetManager_ = assetManager;
    density_ = density;
    ensureThreadStarted();
}

void NativeApp::attachCallbackTarget(JNIEnv* env, jobject target) {
    if (callbackTarget_) {
        env->DeleteGlobalRef(callbackTarget_);
        callbackTarget_ = nullptr;
    }
    callbackTarget_ = env->NewGlobalRef(target);
    jclass cls = env->GetObjectClass(target);
    requestCatalogMethod_ = env->GetMethodID(cls, "onNativeRequestCatalogRefresh", "()V");
    requestImageMethod_ = env->GetMethodID(cls, "onNativeRequestImage", "(JLjava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (!requestCatalogMethod_ || !requestImageMethod_) {
        LOGE("attachCallbackTarget: failed to resolve callback method IDs");
    }
}

void NativeApp::ensureThreadStarted() {
    if (threadRunning_.exchange(true)) return;
    renderThread_ = std::thread([this] { threadMain(); });
}

void NativeApp::postSurfaceCreated(ANativeWindow* window) {
    Command c; c.type = CommandType::SurfaceCreated; c.window = window;
    queue_.push(std::move(c));
}
void NativeApp::postSurfaceChanged(int width, int height) {
    Command c; c.type = CommandType::SurfaceChanged; c.width = width; c.height = height;
    queue_.push(std::move(c));
}
void NativeApp::postSurfaceDestroyed() {
    Command c; c.type = CommandType::SurfaceDestroyed;
    queue_.push(std::move(c));
}
void NativeApp::postPause() {
    Command c; c.type = CommandType::Pause;
    queue_.push(std::move(c));
}
void NativeApp::postResume() {
    Command c; c.type = CommandType::Resume;
    queue_.push(std::move(c));
}
void NativeApp::postDestroy() {
    Command c; c.type = CommandType::Shutdown;
    queue_.push(std::move(c));
}
void NativeApp::postTouch(input::TouchAction action, float x, float y, long timeMs) {
    Command c;
    c.type = CommandType::Touch;
    c.touchAction = action;
    c.x = x;
    c.y = y;
    c.touchTimeMs = timeMs;
    queue_.push(std::move(c));
}
void NativeApp::postCatalogLoaded(std::string json) {
    Command c; c.type = CommandType::CatalogLoaded; c.text = std::move(json);
    queue_.push(std::move(c));
}
void NativeApp::postCatalogFailed(std::string message) {
    Command c; c.type = CommandType::CatalogFailed; c.text = std::move(message);
    queue_.push(std::move(c));
}
void NativeApp::postImageDecoded(long productId, int width, int height, std::vector<uint8_t> pixels) {
    Command c;
    c.type = CommandType::ImageDecoded;
    c.productId = productId;
    c.imageWidth = width;
    c.imageHeight = height;
    c.imagePixels = std::move(pixels);
    queue_.push(std::move(c));
}
void NativeApp::postImageFailed(long productId) {
    Command c; c.type = CommandType::ImageFailed; c.productId = productId;
    queue_.push(std::move(c));
}

void NativeApp::threadMain() {
    if (vm_) vm_->AttachCurrentThread(&threadEnv_, nullptr);

    bool running = true;
    while (running) {
        // While a fling is decaying we need ~60fps wakeups to animate it;
        // otherwise block indefinitely (long timeout) so the thread is truly
        // idle -- no frame-loop work happens with nothing to do. The wait
        // length is computed against nextFlingDeadline_ (a fixed-cadence
        // deadline that advances from itself, not from "now") rather than a
        // flat constant, so command-processing/render time spent each loop
        // doesn't silently stretch the real tick-to-tick interval -- see
        // updateFling() for how the deadline is advanced and corrected if we
        // fall behind. The physics itself (updateFling) doesn't depend on
        // this cadence being exact -- it's evaluated from elapsed wall time
        // regardless of when a tick actually runs -- so pacing jitter here
        // can only affect visual smoothness, never correctness.
        int timeoutMs = 1000;
        if (!paused_ && flingTarget_ != input::FlingTarget::None) {
            auto now = std::chrono::steady_clock::now();
            auto remaining = nextFlingDeadline_ > now ? nextFlingDeadline_ - now : std::chrono::steady_clock::duration::zero();
            timeoutMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        }
        std::vector<Command> batch;
        queue_.drainBlocking(batch, timeoutMs);
        for (auto& cmd : batch) {
            if (cmd.type == CommandType::Shutdown) running = false;
            handleCommand(cmd);
        }
        if (!paused_) {
            updateFling();
            renderIfNeeded();
        }
    }

    if (renderer_.isReady()) renderer_.destroyContext();
    if (vm_ && threadEnv_) {
        vm_->DetachCurrentThread();
        threadEnv_ = nullptr;
    }
    threadRunning_ = false;
}

void NativeApp::handleCommand(Command& cmd) {
    switch (cmd.type) {
        case CommandType::SurfaceCreated: {
            if (renderer_.createContext(cmd.window, assetManager_, density_)) {
                needsRender_ = true;
                recomputeLayout();
                if (!initialLoadRequested_) {
                    initialLoadRequested_ = true;
                    catalogRefreshInFlight_ = true;
                    callJavaRequestCatalogRefresh();
                }
            } else {
                LOGE("Failed to create EGL context on SurfaceCreated");
            }
            break;
        }
        case CommandType::SurfaceChanged: {
            renderer_.resize(cmd.width, cmd.height);
            screenW_ = cmd.width;
            screenH_ = cmd.height;
            recomputeLayout();
            needsRender_ = true;
            break;
        }
        case CommandType::SurfaceDestroyed: {
            renderer_.destroyContext();
            images_.releaseAllTextures();
            needsRender_ = false;
            break;
        }
        case CommandType::Pause: {
            paused_ = true;
            stopFling(); // don't resume a stale fling after an arbitrarily long background period
            break;
        }
        case CommandType::Resume: {
            paused_ = false;
            needsRender_ = true;
            break;
        }
        case CommandType::Shutdown: {
            break; // loop exit handled by caller
        }
        case CommandType::Touch: {
            // Grabbing a scrollable region (anywhere, not just the one
            // currently flinging) always interrupts any momentum in
            // progress, same as every touch scroller behaves.
            if (cmd.touchAction == input::TouchAction::Down) stopFling();

            recomputeLayout();
            input::TouchOutcome outcome = inputController_.onTouch(cmd.touchAction, cmd.x, cmd.y, cmd.touchTimeMs,
                                                                    layout_, filteredProducts_);

            if (outcome.gridScrollDelta != 0) gridScrollY_ += outcome.gridScrollDelta;
            if (outcome.categoryScrollDelta != 0) categoryScrollX_ += outcome.categoryScrollDelta;
            if (outcome.cartScrollDelta != 0) cartScrollX_ += outcome.cartScrollDelta;

            if (outcome.flingTarget != input::FlingTarget::None) {
                startFling(outcome.flingTarget, outcome.flingVelocity);
            }

            using input::ActionType;
            switch (outcome.action.type) {
                case ActionType::SelectCategory:
                    if (outcome.action.categoryIndex != selectedCategoryIndex_) {
                        selectedCategoryIndex_ = outcome.action.categoryIndex;
                        recomputeFilteredProducts();
                        gridScrollY_ = 0;
                    }
                    break;
                case ActionType::TapRefresh:
                    if (!catalogRefreshInFlight_) {
                        catalogRefreshInFlight_ = true;
                        callJavaRequestCatalogRefresh();
                    }
                    break;
                case ActionType::TapClearCart:
                    cart_.clear();
                    break;
                case ActionType::TapProductAdd: {
                    const Product* p = repository_.findById(outcome.action.productId);
                    if (p) cart_.add(*p);
                    break;
                }
                case ActionType::TapCartRemove:
                    cart_.remove(outcome.action.productId);
                    break;
                case ActionType::TapCheckout:
                    if (!cart_.isEmpty()) overlayVisible_ = true;
                    break;
                case ActionType::TapOverlayClose:
                    overlayVisible_ = false;
                    break;
                default:
                    break;
            }

            recomputeLayout();
            if (outcome.needsRedraw || outcome.action.type != ActionType::None) needsRender_ = true;
            break;
        }
        case CommandType::CatalogLoaded: {
            std::string err;
            bool ok = repository_.loadFromJson(cmd.text, &err);
            if (ok) {
                catalogState_ = CatalogLoadState::Loaded;
                recomputeFilteredProducts();
                errorMessage_.clear();
            } else {
                errorMessage_ = err;
                if (repository_.isEmpty()) catalogState_ = CatalogLoadState::Error;
                LOGE("Catalog JSON invalid: %s", err.c_str());
            }
            catalogRefreshInFlight_ = false;
            recomputeLayout();
            needsRender_ = true;
            break;
        }
        case CommandType::CatalogFailed: {
            errorMessage_ = cmd.text;
            if (repository_.isEmpty()) catalogState_ = CatalogLoadState::Error;
            catalogRefreshInFlight_ = false;
            LOGW("Catalog fetch failed: %s", cmd.text.c_str());
            needsRender_ = true;
            break;
        }
        case CommandType::ImageDecoded: {
            images_.onDecoded(cmd.productId, cmd.imageWidth, cmd.imageHeight, std::move(cmd.imagePixels));
            needsRender_ = true;
            break;
        }
        case CommandType::ImageFailed: {
            images_.onFailed(cmd.productId);
            break;
        }
    }
}

void NativeApp::recomputeFilteredProducts() {
    const std::string& category = (selectedCategoryIndex_ >= 0 &&
                                   (size_t)selectedCategoryIndex_ < fixedCategories().size())
                                      ? fixedCategories()[(size_t)selectedCategoryIndex_]
                                      : fixedCategories()[0];
    filteredProducts_ = repository_.filterByCategory(category);
}

void NativeApp::recomputeLayout() {
    // `layoutInputScratch_` and `layout_` are persistent NativeApp members
    // reused across calls (not reconstructed here) specifically so this
    // function -- called on every fling tick, ~60 times a second against a
    // 100+ product catalog -- doesn't churn the heap every frame. Vectors
    // are overwritten/cleared+refilled in place rather than replaced.
    auto buildScratchInput = [&]() -> const layout::LayoutInput& {
        layout::LayoutInput& in = layoutInputScratch_;
        in.screenW = screenW_;
        in.screenH = screenH_;
        in.density = density_;
        in.categories = fixedCategories(); // 5 short strings; stabilizes to zero extra allocation after warm-up
        in.selectedCategoryIndex = selectedCategoryIndex_;
        in.categoryScrollX = categoryScrollX_;
        in.cardCount = (int)filteredProducts_.size();
        in.gridScrollY = gridScrollY_;
        in.cartVisible = !cart_.isEmpty();
        in.cartProductIds.clear();
        for (const auto& line : cart_.lines()) in.cartProductIds.push_back(line.productId);
        in.cartScrollX = cartScrollX_;
        in.overlayVisible = overlayVisible_;
        in.measureCategoryLabel = [this](const std::string& s) {
            return renderer_.measureText(s, gl::FontSize::Regular);
        };
        return in;
    };

    layoutEngine_.recompute(buildScratchInput(), layout_);
    float clampedGrid = std::clamp(gridScrollY_, 0.0f, layout_.gridMaxScrollY);
    float clampedCat = std::clamp(categoryScrollX_, 0.0f, layout_.categoryMaxScrollX);
    float clampedCart = std::clamp(cartScrollX_, 0.0f, layout_.cartMaxScrollX);
    if (clampedGrid != gridScrollY_ || clampedCat != categoryScrollX_ || clampedCart != cartScrollX_) {
        gridScrollY_ = clampedGrid;
        categoryScrollX_ = clampedCat;
        cartScrollX_ = clampedCart;
        layoutEngine_.recompute(buildScratchInput(), layout_);
    }
}

namespace {
// Constant-deceleration ("friction") model: duration and distance both fall
// out of a single tuned deceleration constant, density-scaled like Android's
// own ViewConfiguration scroll friction so it feels consistent across
// devices. A 1200dp/s flick (a brisk but unremarkable flick) travels ~180dp
// over ~300ms; a 3000dp/s flick (a hard flick) travels ~1125dp over ~750ms --
// i.e. distance grows with the *square* of velocity, not linearly, which is
// what makes "short flicks travel short, fast flicks travel much farther"
// actually true rather than a rough approximation.
constexpr float kFlingDecelerationDpPerSec2 = 4000.0f;
constexpr auto kFlingFrameInterval = std::chrono::milliseconds(16); // target cadence, not a guarantee
} // namespace

void NativeApp::startFling(input::FlingTarget target, float velocityPxPerMs) {
    float* scrollMember = target == input::FlingTarget::GridY        ? &gridScrollY_
                          : target == input::FlingTarget::CategoryX ? &categoryScrollX_
                                                                      : &cartScrollX_;
    float decelerationPxPerSec2 = kFlingDecelerationDpPerSec2 * density_;
    float v0PxPerSec = velocityPxPerMs * 1000.0f;

    flingTarget_ = target;
    flingStartOffset_ = *scrollMember;
    flingInitialVelocity_ = v0PxPerSec;
    flingDurationSec_ = std::fabs(v0PxPerSec) / decelerationPxPerSec2;
    flingTotalDistance_ = (v0PxPerSec * v0PxPerSec) / (2.0f * decelerationPxPerSec2) *
                          (v0PxPerSec < 0 ? -1.0f : 1.0f);
    flingStartAt_ = std::chrono::steady_clock::now();
    nextFlingDeadline_ = flingStartAt_ + kFlingFrameInterval;
    flingTickCount_ = 0;

    LOGI("fling start: axis=%d v0=%.1fpx/s duration=%.3fs predictedDistance=%.1fpx density=%.2f",
         (int)target, v0PxPerSec, flingDurationSec_, flingTotalDistance_, density_);
}

void NativeApp::stopFling() {
    if (flingTarget_ != input::FlingTarget::None) {
        float* scrollMember = flingTarget_ == input::FlingTarget::GridY        ? &gridScrollY_
                              : flingTarget_ == input::FlingTarget::CategoryX ? &categoryScrollX_
                                                                                : &cartScrollX_;
        float actualElapsed =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - flingStartAt_).count();
        LOGI("fling end: axis=%d actualElapsed=%.3fs actualDistance=%.1fpx (predicted %.3fs / %.1fpx)",
             (int)flingTarget_, actualElapsed, *scrollMember - flingStartOffset_, flingDurationSec_,
             flingTotalDistance_);
    }
    flingTarget_ = input::FlingTarget::None;
}

void NativeApp::updateFling() {
    if (flingTarget_ == input::FlingTarget::None) return;

    auto now = std::chrono::steady_clock::now();

    // Closed-form kinematics evaluated from total elapsed time since the
    // fling began -- NOT accumulated step by step. A late or irregular tick
    // just evaluates the same formula at a larger t and lands exactly on the
    // correct curve; there is no per-tick integration error to accumulate,
    // and no way for a slow frame to overshoot past where the real curve
    // would be. This is what makes "no single delayed frame creates a large
    // jump" true by construction rather than by tuning a clamp.
    float elapsedSec = std::chrono::duration<float>(now - flingStartAt_).count();
    bool naturallyFinished = elapsedSec >= flingDurationSec_;
    if (naturallyFinished) elapsedSec = flingDurationSec_;

    float decelerationPxPerSec2 = kFlingDecelerationDpPerSec2 * density_;
    float signedDecel = (flingInitialVelocity_ < 0 ? 1.0f : -1.0f) * decelerationPxPerSec2;
    float offset = flingStartOffset_ + flingInitialVelocity_ * elapsedSec + 0.5f * signedDecel * elapsedSec * elapsedSec;

    float* scrollMember = flingTarget_ == input::FlingTarget::GridY        ? &gridScrollY_
                          : flingTarget_ == input::FlingTarget::CategoryX ? &categoryScrollX_
                                                                            : &cartScrollX_;
    *scrollMember = offset;
    recomputeLayout(); // clamps *scrollMember back into range if it overran a bound
    bool hitBound = (*scrollMember != offset);

    needsRender_ = true;

    // Diagnostic trace (~every 4th tick, so a half-second fling produces a
    // handful of lines, not a flood): instantaneous velocity at this exact
    // elapsed time, straight from the closed-form derivative -- lets you
    // confirm from logcat alone that offset advances continuously and
    // |velocity| decreases monotonically through a normal fling.
    if ((flingTickCount_++ % 4) == 0) {
        float instantVelocity = flingInitialVelocity_ + signedDecel * elapsedSec;
        LOGI("fling trace: t=%.3fs offset=%.1f v=%.1fpx/s%s", elapsedSec, *scrollMember, instantVelocity,
             hitBound ? " [bound]" : "");
    }

    if (naturallyFinished || hitBound) {
        stopFling();
        return;
    }

    // Fixed-cadence scheduling: advance from the *previous* deadline, not
    // from "now", so per-tick lateness doesn't compound into permanent
    // drift over a long fling. If we've fallen more than one interval
    // behind (a real stall, not just jitter), resync to now rather than
    // let the loop spin trying to "catch up" through a backlog of instantly
    // due deadlines.
    nextFlingDeadline_ += kFlingFrameInterval;
    if (nextFlingDeadline_ < now) nextFlingDeadline_ = now + kFlingFrameInterval;
}

int NativeApp::visibleCardCountAndRequestImages(bool issueRequests) {
    int visible = 0;
    for (size_t i = 0; i < layout_.cards.size() && i < filteredProducts_.size(); ++i) {
        const layout::CardLayout& card = layout_.cards[i];
        if (card.card.y + card.card.h < layout_.grid.y || card.card.y > layout_.grid.y + layout_.grid.h) {
            continue;
        }
        ++visible;
        const Product* p = filteredProducts_[i];
        if (issueRequests && p && images_.needsRequest(p->id)) {
            images_.markRequested(p->id);
            callJavaRequestImage(p->id, p->thumbnailUrl);
        }
    }
    if (issueRequests) {
        for (const auto& line : cart_.lines()) {
            const Product* p = repository_.findById(line.productId);
            if (p && images_.needsRequest(p->id)) {
                images_.markRequested(p->id);
                callJavaRequestImage(p->id, p->thumbnailUrl);
            }
        }
    }
    return visible;
}

void NativeApp::renderIfNeeded() {
    if (!renderer_.isReady()) return;

    int visibleCount = visibleCardCountAndRequestImages(catalogState_ == CatalogLoadState::Loaded);

    if (!needsRender_) return;
    diagnostics_.beginFrame();

    FrameState state;
    state.layout = &layout_;
    state.repository = &repository_;
    state.cart = &cart_;
    state.images = &images_;
    state.visibleProducts = &filteredProducts_;
    state.selectedCategoryIndex = selectedCategoryIndex_;
    state.catalogState = catalogState_;
    state.errorMessage = errorMessage_;

    renderer_.draw(state);
    diagnostics_.endFrame(visibleCount, images_.decodedCacheBytesApprox(), images_.textureCount());
    needsRender_ = false;
}

void NativeApp::callJavaRequestCatalogRefresh() {
    if (!threadEnv_ || !callbackTarget_ || !requestCatalogMethod_) return;
    threadEnv_->CallVoidMethod(callbackTarget_, requestCatalogMethod_);
    if (threadEnv_->ExceptionCheck()) threadEnv_->ExceptionClear();
}

void NativeApp::callJavaRequestImage(long productId, const std::string& url) {
    if (!threadEnv_ || !callbackTarget_ || !requestImageMethod_) return;
    jstring jurl = threadEnv_->NewStringUTF(url.c_str());
    threadEnv_->CallVoidMethod(callbackTarget_, requestImageMethod_, (jlong)productId, jurl);
    if (threadEnv_->ExceptionCheck()) threadEnv_->ExceptionClear();
    threadEnv_->DeleteLocalRef(jurl);
}

} // namespace kiosk
