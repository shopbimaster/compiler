#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
mkdir -p /tmp/h903_ir

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"

${BUILD_DIR}/compiler "$src" -o /tmp/h903_ir/o1.ir -o1 2>/dev/null
${BUILD_DIR}/compiler "$src" -o /tmp/h903_ir/o2.ir -o2 2>/dev/null

echo "=== o1.ir main function ==="
awk '/^define.*main/,/^end/' /tmp/h903_ir/o1.ir

echo ""
echo "=== o2.ir main function ==="
awk '/^define.*main/,/^end/' /tmp/h903_ir/o2.ir
