#!/usr/bin/env bash
# Isolated native Metal test app. Build with tools/macos/build.sh first.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$REPO/out/Lords of Shadow Metal.app"
case "${1:-play}" in
    package)
        LOS_APP_PATH="$APP" LOS_APP_NAME="Lords of Shadow Metal" \
        LOS_APP_IDENTIFIER=local.lordsofshadow.recompiled.metal LOS_GPU_BACKEND=metal \
        LOS_USER_DATA_ROOT="$REPO/out/metal-user-data/los" \
            "$REPO/tools/macos/package.sh"
        ;;
    play)
        if [[ $# -gt 0 ]]; then shift; fi
        [[ -x "$APP/Contents/MacOS/los" ]] || "$0" package
        mkdir -p "$REPO/out/metal-user-data"
        exec /usr/bin/open "$APP" --env "XDG_DATA_HOME=$REPO/out/metal-user-data" \
            --env "MTL_DEBUG_LAYER=${MTL_DEBUG_LAYER:-0}" \
            --env "LOS_FRAME_TRACE=${LOS_FRAME_TRACE:-}" \
            --env "LOS_METAL_SHADER_DUMP=$REPO/out/metal-shader-errors" \
            --stdout "$REPO/out/metal-game-stdout.log" --stderr "$REPO/out/metal-game-stderr.log" --args "$@"
        ;;
    smoke)
        cmake --build "$REPO/out/mac" --target los_metal_smoke -j "${LOS_BUILD_JOBS:-4}"
        exec env MTL_DEBUG_LAYER=1 "$REPO/out/mac/los_metal_smoke"
        ;;
    *) echo 'Usage: tools/macos/metal.sh [package|play|smoke]' >&2; exit 2 ;;
esac
