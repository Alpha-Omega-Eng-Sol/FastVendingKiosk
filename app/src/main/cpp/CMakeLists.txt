# Fast Vending Kiosk native renderer: EGL/GLES2 UI, JNI bridge, and the
# platform-independent "core" business logic (JSON, product repository,
# cart) that is also exercised by the host-side test suite under /tests.

cmake_minimum_required(VERSION 3.22.1)

project("fastkiosk")

add_library(${CMAKE_PROJECT_NAME} SHARED
    jni_bridge.cpp
    native_app.cpp
    diagnostics.cpp

    core/json.cpp
    core/product_repository.cpp
    core/cart_manager.cpp

    gl/shader.cpp
    gl/quad_batch.cpp
    gl/font_atlas.cpp
    gl/renderer.cpp

    layout/layout_engine.cpp

    input/input_controller.cpp

    image/image_manager.cpp

    ui/checkout_overlay.cpp

    vendor/stb_impl.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)

target_link_libraries(${CMAKE_PROJECT_NAME}
    android
    log
    EGL
    GLESv2
)
