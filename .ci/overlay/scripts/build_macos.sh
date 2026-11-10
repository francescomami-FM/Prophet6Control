#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_PATH="${1:-${JUCE_DIR:-}}"
ARCHS="${CMAKE_OSX_ARCHITECTURES:-arm64}"
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-11.0}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This build script must run on macOS. Current host: $(uname -s)." >&2
  exit 10
fi

if [[ -z "$JUCE_PATH" || ! -f "$JUCE_PATH/CMakeLists.txt" ]]; then
  echo "Usage: $0 /absolute/path/to/JUCE" >&2
  echo "Or set JUCE_DIR to a JUCE source checkout." >&2
  exit 2
fi

cmake -S "$ROOT" -B "$ROOT/build" \
  -DJUCE_DIR="$JUCE_PATH" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET"
cmake --build "$ROOT/build" --config Release --parallel
"$ROOT/scripts/package_dmg_macos.sh"
