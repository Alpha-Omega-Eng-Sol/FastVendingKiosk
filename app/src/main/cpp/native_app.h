#pragma once

#include <android/asset_manager.h>
#include <jni.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "command_queue.h"
#include "core/cart_manager.h"
#include "core/product_repository.h"
#include "diagnostics.h"
#include "gl/renderer.h"
#include "image/image_manager.h"
#include "input/input_controller.h"
#include "layout/layout_engine.h"

namespace kiosk {

// Owns every piece of application state and runs the single dedicated
// render/update thread. All Kotlin-originated calls (lifecycle, touch,
// network results) just push a Command onto the queue and return
// immediately; this class's own thread is the only place that touches GL,
// and the only place business state (catalog, cart, scroll, selection) is
// mutated, so nothing here needs its own locking beyond the command queue.
class NativeApp {
public:
    static NativeApp& instance();

    void init(JavaVM* vm, AAssetManager* assetManager, float density);
    void attachCallbackTarget(JNIEnv* env, jobject target);

    void postSurfaceCreated(ANativeWindow* window);
    void postSurfaceChanged(int width, int height);
    void postSurfaceDestroyed();
    void postPause();
    void postResume();
    void postDestroy();
    void postTouch(input::TouchAction action, float x, float y, long timeMs);
    void postCatalogLoaded(std::string json);
    void postCatalogFailed(std::string message);
    void postImageDecoded(long productId, int width, int height, std::vector<uint8_t> pixels);
    void postImageFailed(long productId);

private:
    NativeApp() = default;
    ~NativeApp();
    NativeApp(const NativeApp&) = delete;

    void ensureThreadStarted();
    void threadMain();
    void handleCommand(Command& cmd);
    void recomputeFilteredProducts();
    void recomputeLayout();
    void renderIfNeeded();
    void updateFling();
    void startFling(input::FlingTarget target, float velocityPxPerMs);
    void stopFling();

    void callJavaRequestCatalogRefresh();
    void callJavaRequestImage(long productId, const std::string& url);
    int visibleCardCountAndRequestImages(bool issueRequests);

    JavaVM* vm_ = nullptr;
    JNIEnv* threadEnv_ = nullptr; // valid only on renderThread_, between Attach/DetachCurrentThread
    AAssetManager* assetManager_ = nullptr;
    float density_ = 1.0f;

    jobject callbackTarget_ = nullptr; // global ref
    jmethodID requestCatalogMethod_ = nullptr;
    jmethodID requestImageMethod_ = nullptr;

    std::thread renderThread_;
    std::atomic<bool> threadRunning_{false};
    CommandQueue queue_;

    // --- state mutated only on the render thread ---
    ProductRepository repository_;
    CartManager cart_;
    ImageManager images_;
    Renderer renderer_;
    input::InputController inputController_;
    layout::LayoutEngine layoutEngine_;
    Diagnostics diagnostics_;

    int screenW_ = 0, screenH_ = 0;
    bool paused_ = false;
    bool needsRender_ = false;
    bool initialLoadRequested_ = false;
    bool catalogRefreshInFlight_ = false;

    int selectedCategoryIndex_ = 0;
    float gridScrollY_ = 0, categoryScrollX_ = 0, cartScrollX_ = 0;
    bool overlayVisible_ = false;

    // Momentum scrolling: constant-deceleration kinematics evaluated in
    // closed form from elapsed wall time since the fling started (see
    // updateFling()), not accumulated step by step -- so an irregularly
    // late tick lands exactly on the correct curve instead of drifting.
    input::FlingTarget flingTarget_ = input::FlingTarget::None;
    std::chrono::steady_clock::time_point flingStartAt_;
    std::chrono::steady_clock::time_point nextFlingDeadline_;
    float flingStartOffset_ = 0;     // scroll offset at the moment the fling began
    float flingInitialVelocity_ = 0; // px/sec, signed
    float flingDurationSec_ = 0;     // time until velocity reaches zero
    float flingTotalDistance_ = 0;   // signed displacement over flingDurationSec_, for diagnostics
    int flingTickCount_ = 0;         // for throttling the mid-fling diagnostic trace

    CatalogLoadState catalogState_ = CatalogLoadState::Loading;
    std::string errorMessage_;

    std::vector<const Product*> filteredProducts_;
    layout::LayoutResult layout_;
    layout::LayoutInput layoutInputScratch_; // reused across recomputeLayout() calls; see recomputeLayout()
};

} // namespace kiosk
