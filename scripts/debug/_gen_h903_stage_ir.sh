#!/bin/bash
# Generate IR at different O2 stages by temporarily disabling later passes
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"

# 用 bisect 截断在 Phase 1 后（mem2reg 已运行）
OPT_BISECT_O2=1 ${BUILD_DIR}/compiler "$src" -o "${PROJECT_DIR}/test/_tmp_h903_p1.ir" -O1 2>/dev/null
echo "Phase 1 IR written to test/_tmp_h903_p1.ir"
