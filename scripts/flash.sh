#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$SCRIPT_DIR/.." || exit 1

FLASH_CLI="$(cd "$SCRIPT_DIR/../../tools" && pwd)/flash_glasses.py"
PYTHON="${FIRMWARE_PYTHON:-$HOME/.local/share/halo-firmware-tools/python/bin/python}"
if [[ ! -x "$PYTHON" ]]; then
    PYTHON="python3"
fi

if ! version_info=$(git describe --tags --long --always --abbrev=8 2>/dev/null) ||
        [[ -z "$version_info" ]]; then
    version_info="unknown"
fi

package="build/H6_APP_${version_info}.7z"
side="right"
port=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --package)
            package="$2"
            shift 2
            ;;
        --port)
            port="$2"
            shift 2
            ;;
        --side)
            side="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--package FILE] [--port /dev/cu.PORT] [--side right|left]"
            exit 0
            ;;
        *)
            echo "未知参数: $1"
            echo "Usage: $0 [--package FILE] [--port /dev/cu.PORT] [--side right|left]"
            exit 1
            ;;
    esac
done

if [[ ! -f "$package" ]]; then
    echo "未找到烧录包: $package"
    echo "先运行: ./scripts/package.sh --product jytek"
    exit 1
fi

if [[ -z "$port" ]]; then
    shopt -s nullglob
    ports=(/dev/cu.usb*)
    shopt -u nullglob
    if [[ ${#ports[@]} -eq 0 ]]; then
        echo "未找到 /dev/cu.usb* 设备"
        exit 1
    fi
    matches=()
    for candidate in "${ports[@]}"; do
        echo "检测: $candidate"
        if info_out="$("$PYTHON" "$FLASH_CLI" info --port "$candidate" 2>&1)"; then
            echo "$info_out"
            if [[ "$info_out" == *"DEVICE side=${side} "* ]]; then
                matches+=("$candidate")
            fi
        else
            echo "$info_out"
        fi
    done
    if [[ ${#matches[@]} -ne 1 ]]; then
        echo "未找到唯一的 ${side} 侧端口"
        exit 1
    fi
    port="${matches[0]}"
fi

echo "烧录包: $package"
echo "端口: $port"
echo "侧: $side"
exec "$PYTHON" "$FLASH_CLI" burn --package "$package" --side "$side" --port "$port"
