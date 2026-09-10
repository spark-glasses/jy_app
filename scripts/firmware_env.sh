#!/usr/bin/env bash

# Add locally installed firmware tools to PATH when the shell cannot find them.
if ! command -v cmake >/dev/null 2>&1 && [[ -x /opt/homebrew/bin/cmake ]]; then
    PATH="/opt/homebrew/bin:$PATH"
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    for firmware_toolchain_bin in \
            "$HOME"/.local/share/halo-firmware-tools/arm-gnu-toolchain-*/bin; do
        if [[ -x "$firmware_toolchain_bin/arm-none-eabi-gcc" ]]; then
            PATH="$firmware_toolchain_bin:$PATH"
            break
        fi
    done
fi

export PATH

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake was not found. Install CMake or add it to PATH." >&2
    return 1
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "arm-none-eabi-gcc was not found. Install the ARM firmware toolchain or add it to PATH." >&2
    return 1
fi
