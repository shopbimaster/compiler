#!/bin/bash
# 重新生成 ANTLR C++ 分析器，覆盖 src/antlr/
# 用法: bash scripts/grammar/regen_antlr.sh
#
# 【版本匹配说明】（踩坑记录，务必先读）
#   本机 CMake 实际链接的 ANTLR C++ runtime 是系统包 4.10
#   （/usr/lib/x86_64-linux-gnu/libantlr4-runtime.so.4.10），
#   其头文件 <antlr4-runtime/...> 只提供 std::once_flag/std::call_once，
#   【没有】 4.13.1 才有的 antlr4::internal::OnceFlag。
#   而 /usr/local/bin/antlr4 指向 4.13.1 jar，用它生成的代码会引用
#   antlr4::internal::OnceFlag → 编译报 "‘antlr4::internal’ has not been declared"。
#   因此必须用与 runtime 匹配的 4.10.1 生成器，即仓库自带 lib/antlr-4.10.1.jar。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
G="$ROOT/grammar"
OUT="$ROOT/src/antlr"
JAR="$ROOT/lib/antlr-4.10.1.jar"

[ -f "$JAR" ] || { echo "缺少生成器 $JAR"; exit 1; }

echo "== 使用生成器（须与系统 runtime 4.10 匹配）=="
echo "  $JAR"

echo "== 1) 先 Lexer 生成（生成 .tokens 供 Parser 使用）=="
java -jar "$JAR" -Dlanguage=Cpp -visitor -o "$OUT" "$G/SysY2022Lexer.g4"

echo "== 2) 再 Parser 生成（依赖 SysY2022Lexer.tokens）=="
cp -f "$OUT/SysY2022Lexer.tokens" "$G/" 2>/dev/null || true
java -jar "$JAR" -Dlanguage=Cpp -visitor -o "$OUT" "$G/SysY2022Parser.g4"

echo "== 2.5) 修复：生成代码用 std::once_flag/std::call_once，需 <mutex> =="
# 4.10.1 生成的 .cpp 用 std::once_flag/std::call_once（标准库，来自 <mutex>），
# 但 include 里没带 <mutex>，统一补上（与 git 基线一致）。
fix_mutex() {
  local f="$1" anchor="$2"
  if ! grep -q '#include <mutex>' "$f"; then
    sed -i "s|#include \"$anchor\"|#include \"$anchor\"\n#include <mutex>|" "$f"
    echo "  patched <mutex> -> $(basename "$f")"
  fi
}
fix_mutex "$OUT/SysY2022Lexer.cpp"  "SysY2022Lexer.h"
fix_mutex "$OUT/SysY2022Parser.cpp" "SysY2022ParserVisitor.h"

echo "== 3) 校验关键生成物 =="
for f in SysY2022Lexer.h SysY2022Lexer.cpp SysY2022Parser.h SysY2022Parser.cpp \
         SysY2022ParserVisitor.h SysY2022ParserBaseVisitor.h \
         SysY2022ParserListener.h SysY2022ParserBaseListener.h; do
  if [ -f "$OUT/$f" ]; then echo "  OK  $f"; else echo "  MISSING $f"; exit 1; fi
done

echo "== 4) 确认用对了生成器（不应出现 antlr4::internal）=="
if grep -q 'antlr4::internal' "$OUT/SysY2022Lexer.cpp"; then
  echo "  错误：仍含 antlr4::internal，说明生成器版本与 runtime 不匹配"; exit 1
else
  echo "  OK  无 antlr4::internal（std::call_once 版本，与 runtime 4.10 匹配）"
fi

echo "== 5) 确认 vectorDecl 已生成 =="
grep -l "VectorDeclContext" "$OUT/SysY2022Parser.h" && echo "  VectorDeclContext OK"
grep -l "visitVectorDecl" "$OUT/SysY2022ParserVisitor.h" && echo "  visitVectorDecl OK"

echo "DONE"
