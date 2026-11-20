#pragma once

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>

#define KIOSK_LOG_TAG "KioskNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, KIOSK_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, KIOSK_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, KIOSK_LOG_TAG, __VA_ARGS__)

#define GL_CHECK(op) \
    do { \
        op; \
        GLenum err = glGetError(); \
        if (err != GL_NO_ERROR) { \
            LOGE("GL error 0x%x after " #op " at %s:%d", err, __FILE__, __LINE__); \
        } \
    } while (0)
