#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"

${BUILD_DIR}/compiler "$src" -o "${PROJECT_DIR}/test/_tmp_h903_o2.ir" -o2 2>/dev/null
echo "IR written to ${PROJECT_DIR}/test/_tmp_h903_o2.ir"
