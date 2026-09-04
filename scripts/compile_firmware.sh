#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"

source "$SCRIPT_DIR/firmware_env.sh"

cmake --build "$PROJECT_DIR/build" \
    --target uimain \
    --parallel "${BUILD_JOBS:-4}"
