#!/bin/bash
#
# build.sh — Build the audio device switch hook for Wine/GPTK on macOS
#
# Builds a universal (arm64 + x86_64) dylib by default.
# Use ARCH=arm64 or ARCH=x86_64 to build for a single architecture.
#

set -euo pipefail
cd "$(dirname "$0")"

SRC="audio_switch_hook.c"
OUT="libaudioswitchhook.dylib"
ARCH="${ARCH:-universal}"

CFLAGS="-Wall -Wextra -O2 -dynamiclib"
FRAMEWORKS="-framework CoreAudio -framework AudioToolbox -framework CoreFoundation"

echo "Building $OUT ..."

if [ "$ARCH" = "universal" ]; then
    echo "  → Universal binary (arm64 + x86_64)"
    clang $CFLAGS -arch arm64 -arch x86_64 $FRAMEWORKS -o "$OUT" "$SRC"
elif [ "$ARCH" = "arm64" ]; then
    echo "  → arm64 only"
    clang $CFLAGS -arch arm64 $FRAMEWORKS -o "$OUT" "$SRC"
elif [ "$ARCH" = "x86_64" ]; then
    echo "  → x86_64 only (for Rosetta/CrossOver Wine)"
    clang $CFLAGS -arch x86_64 $FRAMEWORKS -o "$OUT" "$SRC"
else
    echo "Unknown ARCH=$ARCH; use arm64, x86_64, or universal"
    exit 1
fi

# Sign ad-hoc so DYLD doesn't reject it
codesign -s - --force "$OUT" 2>/dev/null || true

echo "Built: $(pwd)/$OUT"
file "$OUT"
echo ""
echo "Usage:"
echo "  DYLD_INSERT_LIBRARIES=$(pwd)/$OUT wine game.exe"
echo ""
echo "For verbose logging:"
echo "  AUDIO_HOOK_VERBOSE=1 DYLD_INSERT_LIBRARIES=$(pwd)/$OUT wine game.exe"
