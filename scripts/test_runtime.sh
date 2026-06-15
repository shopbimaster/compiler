#!/bin/bash
# ================================================================
# SysY Compiler - Runtime I/O Test
# Usage: ./scripts/test_runtime.sh
# ================================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo '=== Testing: 73_int_io.sy ==='
"${PROJECT_DIR}/build/compiler" -S "${PROJECT_DIR}/test/functional/73_int_io.sy" -o /tmp/test_73_int_io.S -O0 2>&1
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/test_73_int_io /tmp/test_73_int_io.S "${PROJECT_DIR}/build/libsylib.a" 2>&1
echo "--- stdout ---"
qemu-riscv64 /tmp/test_73_int_io < "${PROJECT_DIR}/test/functional/73_int_io.in" 2>/dev/null
echo "--- exit code: $? ---"
echo ""
echo "Expected output:"
cat "${PROJECT_DIR}/test/functional/73_int_io.out"