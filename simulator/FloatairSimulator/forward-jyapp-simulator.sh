#!/usr/bin/env bash

set -euo pipefail

DEVICE=""
PORT=24680
ADB="adb"
CONNECT=""
DEVICES=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Forward a local TCP port to the same port on an Android device.
Compatible with Linux and the default Bash 3.2 included with macOS.

Options:
  -d, --device SERIAL       ADB device serial
  -p, --port PORT           TCP port to forward (default: 24680)
      --adb PATH            Path to the adb executable (default: adb)
  -c, --connect ADDRESS     Run "adb connect ADDRESS" before forwarding
  -h, --help                Show this help

Examples:
  $(basename "$0")
  $(basename "$0") --device emulator-5554
  $(basename "$0") --connect 127.0.0.1:5555 --port 24680
EOF
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

need_value() {
    [ "$#" -ge 2 ] && [ -n "$2" ] || die "$1 requires a value."
}

run_adb() {
    local output

    if ! output="$("$ADB" "$@" 2>&1)"; then
        die "adb $* failed: $output"
    fi

    [ -z "$output" ] || printf '%s\n' "$output"
}

get_adb_devices() {
    local output

    if ! output="$("$ADB" devices 2>&1)"; then
        die "adb devices failed: $output"
    fi

    printf '%s\n' "$output" |
        awk '$2 == "device" { sub(/\r$/, "", $1); print $1 }'
}

select_adb_device() {
    local choice
    local index

    printf 'ADB devices:\n'
    for index in "${!DEVICES[@]}"; do
        printf '  [%d] %s\n' "$((index + 1))" "${DEVICES[$index]}"
    done

    [ -t 0 ] || die "Multiple or unselected devices require --device in non-interactive mode."

    while true; do
        read -r -p "Select device number: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] &&
                ((choice >= 1 && choice <= ${#DEVICES[@]})); then
            DEVICE="${DEVICES[$((choice - 1))]}"
            return
        fi

        printf 'Invalid selection. Enter a number from 1 to %d.\n' "${#DEVICES[@]}"
    done
}

load_adb_devices() {
    local adb_device

    DEVICES=()
    while IFS= read -r adb_device; do
        [ -z "$adb_device" ] || DEVICES[${#DEVICES[@]}]="$adb_device"
    done < <(get_adb_devices)
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -d|--device)
            need_value "$@"
            DEVICE="$2"
            shift 2
            ;;
        -p|--port)
            need_value "$@"
            PORT="$2"
            shift 2
            ;;
        --adb)
            need_value "$@"
            ADB="$2"
            shift 2
            ;;
        -c|--connect)
            need_value "$@"
            CONNECT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "Unknown argument: $1"
            ;;
    esac
done

[[ "$PORT" =~ ^[0-9]+$ ]] || die "Port must be an integer."
((PORT >= 1 && PORT <= 65535)) || die "Port must be between 1 and 65535."
command -v "$ADB" >/dev/null 2>&1 || die "adb executable not found: $ADB"

if [ -n "$CONNECT" ]; then
    printf 'Connecting adb device: %s\n' "$CONNECT"
    run_adb connect "$CONNECT"
fi

load_adb_devices
[ "${#DEVICES[@]}" -gt 0 ] ||
    die "No adb device found. Start the emulator or connect a device, then run: adb devices"

if [ -z "$DEVICE" ]; then
    select_adb_device
fi

printf 'Using adb device: %s\n' "$DEVICE"
printf 'Forwarding local tcp:%s -> device tcp:%s\n' "$PORT" "$PORT"

"$ADB" -s "$DEVICE" forward --remove "tcp:$PORT" >/dev/null 2>&1 || true
run_adb -s "$DEVICE" forward "tcp:$PORT" "tcp:$PORT"

printf '\nCurrent forward list:\n'
run_adb -s "$DEVICE" forward --list
