package com.vendingking.fastkiosk

import android.content.Context
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * Minimal lifecycle/input shell around a native-rendered surface: it owns no
 * drawing logic of its own, just forwards SurfaceHolder callbacks and the
 * primary touch pointer to whatever callbacks the Activity wires up (which,
 * in this app, are the native JNI entry points).
 */
class KioskSurfaceView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {

    var onSurfaceCreatedCb: ((Surface) -> Unit)? = null
    var onSurfaceChangedCb: ((Int, Int) -> Unit)? = null
    var onSurfaceDestroyedCb: (() -> Unit)? = null
    var onTouchCb: ((Int, Float, Float, Long) -> Unit)? = null

    init {
        holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        onSurfaceCreatedCb?.invoke(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        onSurfaceChangedCb?.invoke(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        onSurfaceDestroyedCb?.invoke()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        // Only the primary pointer is tracked -- sufficient for tap-to-add /
        // drag-to-scroll kiosk interaction; secondary pointers are ignored
        // rather than attempting multi-touch gestures.
        val action = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> 0
            MotionEvent.ACTION_UP -> 1
            MotionEvent.ACTION_MOVE -> 2
            MotionEvent.ACTION_CANCEL -> 3
            else -> -1
        }
        if (action >= 0) {
            // A single MOVE callback can batch several real touch-panel
            // samples (event.historySize > 0) when the panel samples faster
            // than the display reports to the app. Forwarding only the
            // final sample throws that resolution away right when it
            // matters most: release-velocity estimation. Replay the
            // historical samples first, each as its own Move, so native
            // code's velocity fit sees the real sample stream.
            if (action == 2) {
                for (h in 0 until event.historySize) {
                    onTouchCb?.invoke(action, event.getHistoricalX(h), event.getHistoricalY(h),
                        event.getHistoricalEventTime(h))
                }
            }
            onTouchCb?.invoke(action, event.x, event.y, event.eventTime)
        }
        return true
    }
}
