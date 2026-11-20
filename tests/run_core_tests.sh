#!/usr/bin/env bash
# Builds and runs the platform-independent core logic tests on the host
# machine (no Android device/emulator or NDK required). Covers JSON parsing,
# discount math, category filtering, and cart operations.
set -euo pipefail
cd "$(dirname "$0")/.."

SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
CXXV1=""
if [ -n "$SDKROOT" ] && [ ! -d "$SDKROOT/usr/include/c++/v1" ]; then
    # Some CommandLineTools installs ship a "MacOSX.sdk" symlink that lacks
    # libc++ headers; fall back to any versioned SDK that has them.
    ALT=$(find /Library/Developer/CommandLineTools/SDKs -maxdepth 1 -iname 'MacOSX*.sdk' \
        -exec test -d '{}/usr/include/c++/v1' \; -print 2>/dev/null | sort -V | tail -1)
    if [ -n "$ALT" ]; then
        SDKROOT="$ALT"
    fi
fi
if [ -n "$SDKROOT" ] && [ -d "$SDKROOT/usr/include/c++/v1" ]; then
    CXXV1="-isysroot $SDKROOT -I $SDKROOT/usr/include/c++/v1"
fi

OUT=/tmp/claude-501/kiosk_core_tests
mkdir -p "$(dirname "$OUT")"

clang++ -std=c++17 -Wall -Wextra $CXXV1 \
  -I app/src/main/cpp/core \
  app/src/main/cpp/core/json.cpp \
  app/src/main/cpp/core/product_repository.cpp \
  app/src/main/cpp/core/cart_manager.cpp \
  tests/test_main.cpp \
  -o "$OUT"

"$OUT"
