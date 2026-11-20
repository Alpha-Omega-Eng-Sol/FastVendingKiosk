package com.vendingking.fastkiosk

import android.app.Activity
import android.content.res.AssetManager
import android.os.Bundle
import android.view.Surface
import android.view.View
import android.view.WindowManager
import java.nio.ByteBuffer

/**
 * Deliberately thin: this Activity owns Android lifecycle plumbing, a
 * SurfaceView to host the native-rendered frame, and the JNI boundary. All
 * product/cart/rendering logic lives in native C++ (see app/src/main/cpp).
 */
class MainActivity : Activity() {

    private lateinit var surfaceView: KioskSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemBars()

        val density = resources.displayMetrics.density
        nativeInit(assets, density)
        nativeAttachCallbackTarget(this)

        surfaceView = KioskSurfaceView(this).apply {
            onSurfaceCreatedCb = { surface -> nativeOnSurfaceCreated(surface) }
            onSurfaceChangedCb = { w, h -> nativeOnSurfaceChanged(w, h) }
            onSurfaceDestroyedCb = { nativeOnSurfaceDestroyed() }
            onTouchCb = { action, x, y, eventTime -> nativeOnTouch(action, x, y, eventTime) }
        }
        setContentView(surfaceView)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    private fun hideSystemBars() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
            )
    }

    override fun onPause() {
        super.onPause()
        nativeOnPause()
    }

    override fun onResume() {
        super.onResume()
        nativeOnResume()
    }

    override fun onDestroy() {
        nativeOnDestroy()
        super.onDestroy()
    }

    // --- Invoked BY native code (render thread), not `external` ---
    // Native owns the decision of *when* a refresh/image fetch is needed;
    // these just perform the I/O and hand the result back over JNI.

    @Suppress("unused") // called via JNI CallVoidMethod from native_app.cpp
    fun onNativeRequestCatalogRefresh() {
        NetworkClient.fetchCatalog(
            onResult = { json -> nativeOnCatalogLoaded(json) },
            onError = { message -> nativeOnCatalogFailed(message) }
        )
    }

    @Suppress("unused") // called via JNI CallVoidMethod from native_app.cpp
    fun onNativeRequestImage(productId: Long, url: String) {
        NetworkClient.fetchThumbnail(
            productId, url,
            onResult = { w, h, buffer -> nativeOnImageDecoded(productId, w, h, buffer) },
            onError = { nativeOnImageFailed(productId) }
        )
    }

    // --- JNI bridge ---

    external fun nativeInit(assetManager: AssetManager, density: Float)
    external fun nativeAttachCallbackTarget(target: Any)
    external fun nativeOnSurfaceCreated(surface: Surface)
    external fun nativeOnSurfaceChanged(width: Int, height: Int)
    external fun nativeOnSurfaceDestroyed()
    external fun nativeOnPause()
    external fun nativeOnResume()
    external fun nativeOnDestroy()
    external fun nativeOnTouch(action: Int, x: Float, y: Float, eventTimeMs: Long)
    external fun nativeOnCatalogLoaded(json: String)
    external fun nativeOnCatalogFailed(message: String)
    external fun nativeOnImageDecoded(productId: Long, width: Int, height: Int, pixels: ByteBuffer)
    external fun nativeOnImageFailed(productId: Long)

    companion object {
        init {
            System.loadLibrary("fastkiosk")
        }
    }
}
