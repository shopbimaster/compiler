#!/bin/bash
# 《前端+显式长度》 用 ANTLR 4.13.1 生成器重新生成 src/antlr（对齐云端 runtime 4.13.1 + clang++）
# 在 Ubuntu-24.04-eval WSL VM 内执行：
#   wsl -d Ubuntu-24.04-eval -e bash -lc "bash /mnt/d/VSCodeProjects/compiler/scripts/grammar/regen_antlr_4131.sh"
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
G="$ROOT/grammar"
OUT="$ROOT/src/antlr"
JAR="/usr/local/lib/antlr-4.13.1-complete.jar"

[ -f "$JAR" ] || { echo "缺少生成器 $JAR"; exit 1; }
echo "== 生成器（4.13.1，对齐云端）==: $JAR"

echo "== 1) Lexer 生成 =="
java -jar "$JAR" -Dlanguage=Cpp -visitor -o "$OUT" "$G/SysY2022Lexer.g4"

echo "== 2) Parser 生成（依赖 Lexer.tokens）=="
cp -f "$OUT/SysY2022Lexer.tokens" "$G/" 2>/dev/null || true
java -jar "$JAR" -Dlanguage=Cpp -visitor -o "$OUT" "$G/SysY2022Parser.g4"

echo "== 3) 校验生成物 =="
for f in SysY2022Lexer.h SysY2022Lexer.cpp SysY2022Parser.h SysY2022Parser.cpp \
         SysY2022ParserVisitor.h SysY2022ParserBaseVisitor.h \
         SysY2022ParserListener.h SysY2022ParserBaseListener.h; do
  [ -f "$OUT/$f" ] && echo "  OK  $f" || { echo "  MISSING $f"; exit 1; }
done

echo "== 4) 确认是 4.13.1 生成（应含 antlr4::internal）=="
grep -q 'antlr4::internal' "$OUT/SysY2022Lexer.cpp" \
  && echo "  OK  含 antlr4::internal（4.13.1，与云端 runtime 匹配）" \
  || echo "  警告：无 antlr4::internal，疑似旧生成器"

echo "== 5) 确认 vecDecl 已生成（显式长度向量分支专用）=="
grep -l "VecDeclContext" "$OUT/SysY2022Parser.h" && echo "  VecDeclContext OK"
grep -l "visitVecDecl" "$OUT/SysY2022ParserVisitor.h" && echo "  visitVecDecl OK"
echo "  VEC_N token number: $(grep -c 'VEC_N' "$OUT/SysY2022Lexer.h" || echo 0)"
echo "REGEN_4131_DONE"
