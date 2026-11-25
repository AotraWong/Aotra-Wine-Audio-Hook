#!/bin/bash
#
# install_hook.sh — Install the audio switch hook for Whisky
#
# This script:
# 1. Builds the dylib if not already built
# 2. Copies it to a stable location
# 3. Shows instructions for integrating with Whisky
#

set -euo pipefail
cd "$(dirname "$0")"

DYLIB="libaudioswitchhook.dylib"
INSTALL_DIR="$HOME/.local/lib"

# Build if needed
if [ ! -f "$DYLIB" ]; then
    echo "Building $DYLIB..."
    bash build.sh
fi

# Install to ~/.local/lib
mkdir -p "$INSTALL_DIR"
cp -f "$DYLIB" "$INSTALL_DIR/$DYLIB"
echo "✓ Installed to: $INSTALL_DIR/$DYLIB"
echo ""

# Check if Whisky data exists
WHISKY_DIR="$HOME/Library/Application Support/com.franke.Whisky"
if [ ! -d "$WHISKY_DIR" ]; then
    WHISKY_DIR="$HOME/Library/Application Support/com.isaacmarovitz.Whisky"
fi

if [ -d "$WHISKY_DIR" ]; then
    echo "✓ Found Whisky at: $WHISKY_DIR"
    echo ""
fi

cat << 'EOF'
╔══════════════════════════════════════════════════════════════╗
║           Audio Device Switch Hook for Whisky               ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  To use this hook, add the following environment variable    ║
║  in your Whisky bottle settings:                             ║
║                                                              ║
║  Key:   DYLD_INSERT_LIBRARIES                                ║
EOF
echo "║  Value: $INSTALL_DIR/$DYLIB"
cat << 'EOF'
║                                                              ║
║  Steps in Whisky:                                            ║
║  1. Open Whisky                                              ║
║  2. Right-click your bottle → Settings                       ║
║  3. Go to "Environment" tab                                  ║
║  4. Add the environment variable above                       ║
║  5. Restart the game                                         ║
║                                                              ║
║  For verbose logging, also add:                              ║
║  Key:   AUDIO_HOOK_VERBOSE                                   ║
║  Value: 1                                                    ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
EOF
