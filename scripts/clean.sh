#!/bin/bash
# ================================================================
# SysY Compiler - 项目清理脚本
# 清理构建产物、临时文件、测试中间文件、缓存
# Usage: ./scripts/clean.sh [--all] [--build] [--temp] [--antlr]
# ================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

MODE="${1:-all}"

# ================================================================
# 清理构建产物
# ================================================================
clean_build() {
    echo -e "${YELLOW}[clean] 清理构建产物...${NC}"

    # CMake 构建目录
    if [ -d "${PROJECT_DIR}/build" ]; then
        rm -rf "${PROJECT_DIR}/build"
        echo "  - build/ (已删除)"
    fi

    # 根目录 CMake 残留
    local cmake_files=(
        "CMakeCache.txt"
        "cmake_install.cmake"
        "Makefile"
    )
    for f in "${cmake_files[@]}"; do
        if [ -f "${PROJECT_DIR}/$f" ]; then
            rm -f "${PROJECT_DIR}/$f"
            echo "  - $f (已删除)"
        fi
    done

    if [ -d "${PROJECT_DIR}/CMakeFiles" ]; then
        rm -rf "${PROJECT_DIR}/CMakeFiles"
        echo "  - CMakeFiles/ (已删除)"
    fi

    echo -e "${GREEN}  构建产物清理完成${NC}"
}

# ================================================================
# 清理临时测试文件
# ================================================================
clean_temp() {
    echo -e "${YELLOW}[clean] 清理临时测试文件...${NC}"

    # /tmp 下的测试文件
    local patterns=(
        "/tmp/sysy_test_*"
        "/tmp/func_test_*"
        "/tmp/hfunc_test_*"
        "/tmp/perf_test_*"
        "/tmp/qemu_test_*"
        "/tmp/quick_*"
        "/tmp/_test_*"
        "/tmp/_t_*"
        "/tmp/t87*"
        "/tmp/test_73*"
        "/tmp/large_stack*"
        "/tmp/sysy-test"
    )
    for pattern in "${patterns[@]}"; do
        # shellcheck disable=SC2086
        if ls $pattern 2>/dev/null | head -1 > /dev/null 2>&1; then
            rm -rf $pattern
            echo "  - $pattern (已删除)"
        fi
    done

    # ~/tmp 下的调试中间文件
    if [ -d ~/tmp ]; then
        rm -rf ~/tmp
        echo "  - ~/tmp/ (已删除)"
    fi
    if [ -d ~/tmp2 ]; then
        rm -rf ~/tmp2
        echo "  - ~/tmp2/ (已删除)"
    fi
    if [ -d ~/tmp3 ]; then
        rm -rf ~/tmp3
        echo "  - ~/tmp3/ (已删除)"
    fi

    echo -e "${GREEN}  临时文件清理完成${NC}"
}

# ================================================================
# 清理 ANTLR 生成文件
# ================================================================
clean_antlr() {
    echo -e "${YELLOW}[clean] 清理 ANTLR 生成文件...${NC}"

    if [ -d "${PROJECT_DIR}/src/antlr" ]; then
        rm -rf "${PROJECT_DIR}/src/antlr"
        echo "  - src/antlr/ (已删除)"
    fi

    # ANTLR 中间文件
    local antlr_patterns=("*.interp" "*.tokens" "*.class")
    for pattern in "${antlr_patterns[@]}"; do
        find "${PROJECT_DIR}" -maxdepth 3 -name "$pattern" -delete 2>/dev/null || true
    done
    echo "  - *.interp / *.tokens / *.class (已删除)"

    echo -e "${GREEN}  ANTLR 生成文件清理完成${NC}"
}

# ================================================================
# 清理编译中间文件
# ================================================================
clean_objects() {
    echo -e "${YELLOW}[clean] 清理编译中间文件...${NC}"

    find "${PROJECT_DIR}" -maxdepth 4 -name "*.o" -delete 2>/dev/null || true
    find "${PROJECT_DIR}" -maxdepth 4 -name "*.obj" -delete 2>/dev/null || true
    find "${PROJECT_DIR}" -maxdepth 4 -name "*.elf" -delete 2>/dev/null || true
    echo "  - *.o / *.obj / *.elf (已删除)"

    echo -e "${GREEN}  编译中间文件清理完成${NC}"
}

# ================================================================
# 主流程
# ================================================================
case "$MODE" in
    --build)
        clean_build
        ;;
    --temp)
        clean_temp
        ;;
    --antlr)
        clean_antlr
        ;;
    --objects)
        clean_objects
        ;;
    all|--all)
        echo "============================================================"
        echo "  SysY Compiler - 项目清理"
        echo "============================================================"
        echo ""
        clean_build
        echo ""
        clean_temp
        echo ""
        clean_antlr
        echo ""
        clean_objects
        echo ""
        echo "============================================================"
        echo -e "  ${GREEN}清理完成${NC}"
        echo "============================================================"
        ;;
    *)
        echo "Usage: $0 [--all|--build|--temp|--antlr|--objects]"
        echo ""
        echo "  --all       清理所有（默认）"
        echo "  --build     仅清理构建产物（build/）"
        echo "  --temp      仅清理临时测试文件"
        echo "  --antlr     仅清理 ANTLR 生成文件"
        echo "  --objects   仅清理编译中间文件"
        echo ""
        exit 1
        ;;
esac