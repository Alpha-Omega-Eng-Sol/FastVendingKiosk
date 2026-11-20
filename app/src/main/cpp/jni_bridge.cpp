// JNI boundary: thin glue between the Kotlin Activity/SurfaceView and
// NativeApp. Every function here just converts JNI types and immediately
// forwards to NativeApp, which queues the call for the render thread -- no
// business logic lives in this file.
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <vector>

#include "gl/gl_common.h"
#include "native_app.h"

using kiosk::NativeApp;
using kiosk::input::TouchAction;

namespace {

std::string jstringToStd(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string result(chars ? chars : "");
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return result;
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeInit(JNIEnv* env, jobject /*thiz*/,
                                                        jobject assetManagerJava, jfloat density) {
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    AAssetManager* assetManager = AAssetManager_fromJava(env, assetManagerJava);
    NativeApp::instance().init(vm, assetManager, density);
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeAttachCallbackTarget(JNIEnv* env, jobject /*thiz*/,
                                                                       jobject target) {
    NativeApp::instance().attachCallbackTarget(env, target);
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnSurfaceCreated(JNIEnv* env, jobject /*thiz*/,
                                                                    jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface); // acquires a reference
    if (!window) {
        LOGE("ANativeWindow_fromSurface returned null");
        return;
    }
    NativeApp::instance().postSurfaceCreated(window); // ownership transferred; Renderer releases it
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnSurfaceChanged(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                    jint width, jint height) {
    NativeApp::instance().postSurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnSurfaceDestroyed(JNIEnv* /*env*/, jobject /*thiz*/) {
    NativeApp::instance().postSurfaceDestroyed();
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnPause(JNIEnv* /*env*/, jobject /*thiz*/) {
    NativeApp::instance().postPause();
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnResume(JNIEnv* /*env*/, jobject /*thiz*/) {
    NativeApp::instance().postResume();
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnDestroy(JNIEnv* /*env*/, jobject /*thiz*/) {
    NativeApp::instance().postDestroy();
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnTouch(JNIEnv* /*env*/, jobject /*thiz*/, jint action,
                                                          jfloat x, jfloat y, jlong eventTimeMs) {
    NativeApp::instance().postTouch(static_cast<TouchAction>(action), x, y, static_cast<long>(eventTimeMs));
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnCatalogLoaded(JNIEnv* env, jobject /*thiz*/, jstring json) {
    NativeApp::instance().postCatalogLoaded(jstringToStd(env, json));
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnCatalogFailed(JNIEnv* env, jobject /*thiz*/,
                                                                   jstring message) {
    NativeApp::instance().postCatalogFailed(jstringToStd(env, message));
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnImageDecoded(JNIEnv* env, jobject /*thiz*/,
                                                                  jlong productId, jint width, jint height,
                                                                  jobject pixels) {
    void* addr = env->GetDirectBufferAddress(pixels);
    jlong capacity = env->GetDirectBufferCapacity(pixels);
    if (!addr || capacity <= 0) {
        LOGE("nativeOnImageDecoded: invalid direct buffer for product %lld", (long long)productId);
        return;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(addr);
    std::vector<uint8_t> copy(bytes, bytes + capacity);
    NativeApp::instance().postImageDecoded(static_cast<long>(productId), width, height, std::move(copy));
}

JNIEXPORT void JNICALL
Java_com_vendingking_fastkiosk_MainActivity_nativeOnImageFailed(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                 jlong productId) {
    NativeApp::instance().postImageFailed(static_cast<long>(productId));
}

} // extern "C"
