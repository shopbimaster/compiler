#!/bin/bash
# 静态指标对比：编译期确定性指标，零运行噪声。实验用，可删。
# 对每个 perf 用例编译成汇编，统计 4 个版本的：
#   - 总指令数（近似：非标签、非指示符的代码行）
#   - mv/fmv.s 拷贝指令数（coalescing 目标）
#   - spill 访存数（栈相关 load/store，反映寄存器压力）
# 判读：GVN 若有益，总指令数应下降；若加重寄存器压力，spill 应上升。
#        coalescing 若有效，mv 数应下降。
DST="$HOME/compiler"
cd "$DST" || exit 1
C=./build/compiler
PERF=test/performance
OUT=/tmp/metrics
rm -rf "$OUT"; mkdir -p "$OUT"

# 统计一个汇编文件的三个指标
count_metrics() {
  local f="$1"
  # 指令行：以空白开头、含助记符、排除标签(:)、指示符(.)、注释(#)
  local insn mv spill
  insn=$(grep -cE '^[[:space:]]+[a-z]' "$f" 2>/dev/null)
  mv=$(grep -cE '^[[:space:]]+(mv|fmv\.s)[[:space:]]' "$f" 2>/dev/null)
  # spill：栈访存。启发式——含 sp 或 s0 偏移的 load/store
  spill=$(grep -cE '^[[:space:]]+(lw|sw|ld|sd|flw|fsw)[[:space:]].*\((sp|s0)\)' "$f" 2>/dev/null)
  echo "$insn $mv $spill"
}

# 运行一个版本：编译所有用例，汇总三指标
run_version() {
  local label="$1"; shift
  local env="$1"
  local tot_insn=0 tot_mv=0 tot_spill=0 ok=0
  for sy in "$PERF"/*.sy; do
    local name; name="$(basename "$sy" .sy)"
    local asm="$OUT/${label}_${name}.s"
    if env $env "$C" -S -o "$asm" "$sy" -O1 >/dev/null 2>&1; then
      ok=$((ok+1))
      read i m s <<< "$(count_metrics "$asm")"
      tot_insn=$((tot_insn + i))
      tot_mv=$((tot_mv + m))
      tot_spill=$((tot_spill + s))
    fi
  done
  printf "%-4s compiled=%-3d  insn=%-7d  mv=%-6d  spill=%-6d\n" \
    "$label" "$ok" "$tot_insn" "$tot_mv" "$tot_spill"
  echo "$tot_insn $tot_mv $tot_spill" > "$OUT/${label}.tot"
}

echo "=========================================================="
echo " 静态指标对比（编译期，零运行噪声）"
echo "=========================================================="
run_version "V0" "RA_COALESCE_MODE=rmw"
run_version "V1" "RA_COALESCE_MODE=full"
run_version "V2" "RA_COALESCE_MODE=rmw  OPT_ENABLE_GVN=1"
run_version "V3" "RA_COALESCE_MODE=full OPT_ENABLE_GVN=1"
echo "----------------------------------------------------------"

read i0 m0 s0 < "$OUT/V0.tot"
read i1 m1 s1 < "$OUT/V1.tot"
read i2 m2 s2 < "$OUT/V2.tot"
read i3 m3 s3 < "$OUT/V3.tot"
echo " 指标         V0(基线)  V1(coal)  V2(GVN)   V3(coal+GVN)"
printf " 总指令数     %-9d %-9d %-9d %-9d\n" "$i0" "$i1" "$i2" "$i3"
printf " mv 拷贝数    %-9d %-9d %-9d %-9d\n" "$m0" "$m1" "$m2" "$m3"
printf " spill 访存   %-9d %-9d %-9d %-9d\n" "$s0" "$s1" "$s2" "$s3"
echo "----------------------------------------------------------"
echo " 关键对比："
echo "   coalescing 消 mv:     V1 vs V0 = $((m1-m0))   (负=有效)"
echo "   coalescing 减 spill:  V1 vs V0 = $((s1-s0))"
echo "   GVN 减总指令(旧coal): V2 vs V0 = $((i2-i0))   (负=GVN有益)"
echo "   GVN 加 spill(旧coal): V2 vs V0 = $((s2-s0))   (正=寄存器压力增)"
echo "   coal 是否缓解GVN压力: V3-spill vs V2-spill = $((s3-s2))  (负=coal缓解了GVN压力)"
echo "   coal+GVN 净指令:      V3 vs V1 = $((i3-i1))   (负=GVN在coal基础上减指令)"
echo "STATIC_EXIT=0"
