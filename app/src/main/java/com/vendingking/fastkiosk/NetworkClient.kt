package com.vendingking.fastkiosk

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * All networking for the app: the dummyjson catalog fetch and per-thumbnail
 * downloads. Uses only platform HttpURLConnection/BitmapFactory (no TLS/HTTP
 * implemented from scratch, no new third-party dependency). Everything here
 * runs on background executors -- nothing ever touches the render thread --
 * and results are handed back to native code via the JNI entry points on
 * MainActivity, which queue them for the render thread to consume.
 */
object NetworkClient {
    private const val TAG = "KioskNetwork"

    // dummyjson's default /products response is paginated to 30 items;
    // limit=0 asks it to return the full catalog (194 products as of this
    // writing), which is what exercises the "100+ products" requirements
    // around bounded image/texture caching and grid virtualization.
    private const val CATALOG_URL = "https://dummyjson.com/products?limit=0"

    private const val CONNECT_TIMEOUT_MS = 8000
    private const val READ_TIMEOUT_MS = 8000

    // Thumbnails are downsampled to this on their longest edge before ever
    // reaching native code, bounding per-image memory regardless of the
    // source image's resolution.
    private const val MAX_THUMBNAIL_DIMENSION = 160

    // Single-thread executor for the catalog (only one fetch is ever
    // meaningful at a time) plus a small bounded pool for thumbnails, so a
    // scroll burst can't spawn unbounded threads/connections.
    private val catalogExecutor = Executors.newSingleThreadExecutor()
    private val imageExecutor = Executors.newFixedThreadPool(4)
    private val catalogInFlight = AtomicBoolean(false)

    fun fetchCatalog(onResult: (String) -> Unit, onError: (String) -> Unit) {
        if (!catalogInFlight.compareAndSet(false, true)) {
            Log.d(TAG, "Catalog fetch already in flight; ignoring duplicate request")
            return
        }
        catalogExecutor.execute {
            try {
                val json = httpGetString(CATALOG_URL)
                onResult(json)
            } catch (e: Exception) {
                Log.w(TAG, "Catalog fetch failed", e)
                onError(e.message ?: e.javaClass.simpleName ?: "Unknown network error")
            } finally {
                catalogInFlight.set(false)
            }
        }
    }

    fun fetchThumbnail(
        productId: Long,
        urlString: String,
        onResult: (width: Int, height: Int, pixels: ByteBuffer) -> Unit,
        onError: () -> Unit
    ) {
        if (urlString.isBlank()) {
            onError()
            return
        }
        imageExecutor.execute {
            var connection: HttpURLConnection? = null
            var bitmap: Bitmap? = null
            try {
                val url = URL(urlString)
                connection = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = CONNECT_TIMEOUT_MS
                    readTimeout = READ_TIMEOUT_MS
                    doInput = true
                }
                connection.connect()
                if (connection.responseCode !in 200..299) {
                    throw IOException("HTTP ${connection.responseCode}")
                }
                val bytes = connection.inputStream.use { it.readBytes() }

                val boundsOptions = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size, boundsOptions)
                val sampleSize = computeInSampleSize(boundsOptions.outWidth, boundsOptions.outHeight, MAX_THUMBNAIL_DIMENSION)

                val decodeOptions = BitmapFactory.Options().apply { inSampleSize = sampleSize }
                bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, decodeOptions)
                    ?: throw IOException("Bitmap decode returned null")

                val longestEdge = maxOf(bitmap.width, bitmap.height)
                if (longestEdge > MAX_THUMBNAIL_DIMENSION) {
                    val scale = MAX_THUMBNAIL_DIMENSION.toFloat() / longestEdge
                    val targetW = maxOf(1, (bitmap.width * scale).toInt())
                    val targetH = maxOf(1, (bitmap.height * scale).toInt())
                    val scaled = Bitmap.createScaledBitmap(bitmap, targetW, targetH, true)
                    if (scaled !== bitmap) {
                        bitmap.recycle()
                        bitmap = scaled
                    }
                }
                if (bitmap.config != Bitmap.Config.ARGB_8888) {
                    val converted = bitmap.copy(Bitmap.Config.ARGB_8888, false)
                    bitmap.recycle()
                    bitmap = converted
                }

                val width = bitmap.width
                val height = bitmap.height
                // Android's ARGB_8888 is laid out in memory as R,G,B,A per
                // pixel, matching the RGBA8 texture format the native
                // renderer expects -- no channel swizzle needed.
                val buffer = ByteBuffer.allocateDirect(width * height * 4)
                bitmap.copyPixelsToBuffer(buffer)
                buffer.rewind()

                onResult(width, height, buffer)
            } catch (e: Exception) {
                Log.w(TAG, "Thumbnail fetch failed for product $productId: ${e.message}")
                onError()
            } finally {
                bitmap?.recycle()
                connection?.disconnect()
            }
        }
    }

    private fun httpGetString(urlString: String): String {
        val connection = URL(urlString).openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = CONNECT_TIMEOUT_MS
            connection.readTimeout = READ_TIMEOUT_MS
            connection.requestMethod = "GET"
            connection.connect()
            if (connection.responseCode !in 200..299) {
                throw IOException("HTTP ${connection.responseCode}")
            }
            return connection.inputStream.use { stream ->
                BufferedReader(InputStreamReader(stream, Charsets.UTF_8)).use { it.readText() }
            }
        } finally {
            connection.disconnect()
        }
    }

    private fun computeInSampleSize(width: Int, height: Int, targetSize: Int): Int {
        if (width <= 0 || height <= 0) return 1
        var sampleSize = 1
        var w = width
        var h = height
        while (w / 2 >= targetSize && h / 2 >= targetSize) {
            sampleSize *= 2
            w /= 2
            h /= 2
        }
        return sampleSize
    }
}
