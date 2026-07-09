#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
mkdir -p /tmp/h903_ir2

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"

${BUILD_DIR}/compiler "$src" -o /tmp/h903_ir2/o2.ir -o2 2>/dev/null
echo "=== O2 IR main function (full) ==="
awk '/^define.*main/,/^end/' /tmp/h903_ir2/o2.ir
