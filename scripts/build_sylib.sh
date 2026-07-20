#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
RISCV_GCC="${RISCV_GCC:-riscv64-linux-gnu-gcc}"
RISCV_AR="${RISCV_AR:-riscv64-linux-gnu-ar}"

if ! command -v "$RISCV_GCC" >/dev/null 2>&1; then
    echo "Error: RISC-V compiler not found: $RISCV_GCC" >&2
    exit 1
fi

if ! command -v "$RISCV_AR" >/dev/null 2>&1; then
    echo "Error: RISC-V archiver not found: $RISCV_AR" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"

"$RISCV_GCC" -O2 -march=rv64gc -mabi=lp64d -mcmodel=medany \
    -c "$PROJECT_DIR/SysYlib/sylib.c" \
    -o "$BUILD_DIR/sylib.o"
"$RISCV_AR" rcs "$BUILD_DIR/libsylib.a" "$BUILD_DIR/sylib.o"

echo "Built $BUILD_DIR/libsylib.a"
