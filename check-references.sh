#!/bin/bash
# 检查项目引用问题的脚本

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
echo "检查项目引用..."

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 需要检查的已删除文件
DELETED_FILES=(
    "include/frontend/AST.h"
    "include/frontend/ASTBuilder.h"
    "include/frontend/SemanticAnalyzer.h"
)

echo ""
echo "检查是否有文件引用已删除的文件..."

# 检查所有 .h 和 .cpp 文件
for file in $(find "$PROJECT_ROOT/include" "$PROJECT_ROOT/src" -type f \( -name "*.h" -o -name "*.cpp" \) 2>/dev/null); do
    for deleted in "${DELETED_FILES[@]}"; do
        # 获取文件名（不含路径）
        deleted_name=$(basename "$deleted")
        if grep -q "#include.*$deleted_name" "$file"; then
            echo -e "${RED}✗ $file 引用了已删除的 $deleted_name${NC}"
        fi
    done
done

echo ""
echo "检查是否有缺少的文件..."

# 检查被引用但不存在的头文件
for file in $(find "$PROJECT_ROOT/include" "$PROJECT_ROOT/src" -type f \( -name "*.h" -o -name "*.cpp" \) 2>/dev/null); do
    while read -r line; do
        if [[ "$line" == *#include* ]]; then
            # 提取头文件名
            header=$(echo "$line" | grep -o '".*"' | tr -d '"' | head -1)
            if [ -n "$header" ]; then
                # 检查是否在 include/ 目录下存在
                if [ ! -f "$PROJECT_ROOT/include/$header" ]; then
                    # 检查是否是标准库或者相对路径
                    if [[ "$header" != *"/"* ]]; then
                        if [ ! -f "$PROJECT_ROOT/include/$header" ]; then
                            echo -e "${YELLOW}⚠ $file 包含 $header (可能在别处生成)${NC}"
                        fi
                    fi
                fi
            fi
        fi
    done < "$file"
done

echo ""
echo -e "${GREEN}检查完成${NC}"
