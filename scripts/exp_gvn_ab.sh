#!/bin/bash
# ================================================================
# [EXP-SCAFFOLD] A/B 实验：coalescing × GVN 交叉对比
# ----------------------------------------------------------------
# 验证 v3.2.0 的 coalescing 增强是否已缓解 GVN 的活跃区间压力，
# 使被禁用的 GVN 可以安全重启。
#
# 4 个版本（同一二进制，靠环境变量切换）：
#   V0  RA_COALESCE_MODE=rmw               GVN off   = v3.1.0 基线
#   V1  RA_COALESCE_MODE=full (默认)        GVN off   = v3.2.0 当前
#   V2  RA_COALESCE_MODE=rmw   OPT_ENABLE_GVN=1       复现 +1319ms 回退
#   V3  RA_COALESCE_MODE=full  OPT_ENABLE_GVN=1       coalescing + GVN
#
# 判读：
#   V3 < V1  → coalescing 解锁 GVN，P2 可落地（下一步真正收益点）
#   V3 ≈ V1, V2 慢 → 部分缓解，需 P1 深化 spill 模型
#   V3 > V1  → 不足，需更强的活跃区间收缩手段
#
# 用法：bash scripts/exp_gvn_ab.sh [重复次数，默认3]
# 必须在 Linux（含 riscv64-linux-gnu-gcc + qemu-riscv64）上运行。
# ================================================================
set -u

REPS="${1:-3}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
COMPILER="${BUILD_DIR}/compiler"
SYLIB="${BUILD_DIR}/libsylib.a"
PERF_DIR="${PROJECT_DIR}/test/performance"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
WORK="$(mktemp -d)"
TIMEOUT_SEC=20

trap 'rm -rf "$WORK"' EXIT

for tool in "$COMPILER" "$GCC" "$QEMU"; do
    command -v "$tool" >/dev/null 2>&1 || [ -x "$tool" ] || {
        echo "ERROR: 缺少 $tool（先 cmake --build build 并装好交叉工具链）" >&2; exit 1; }
done
[ -f "$SYLIB" ] || { echo "ERROR: 缺少 $SYLIB，先 bash scripts/build_sylib.sh" >&2; exit 1; }

# 纳秒计时，输出毫秒
now_ms() { date +%s%3N; }

# 运行一个版本（给定环境变量），返回：编译成功数 / 运行正确数 / 总运行耗时ms
run_version() {
    local label="$1"; shift
    local envassign="$1"   # 形如 "RA_COALESCE_MODE=full OPT_ENABLE_GVN=1"
    local compile_ok=0 run_ok=0 run_bad=0
    local total_ms=0

    for sy in "$PERF_DIR"/*.sy; do
        local name; name="$(basename "$sy" .sy)"
        local asm="$WORK/${name}.s" exe="$WORK/${name}.exe"
        local infile="$PERF_DIR/${name}.in"
        local expected="$PERF_DIR/${name}.out"

        # 编译（优化开关经环境变量注入）
        if ! env $envassign "$COMPILER" -S -o "$asm" "$sy" -O1 >/dev/null 2>&1; then
            continue
        fi
        compile_ok=$((compile_ok+1))
        # 链接
        if ! "$GCC" -march=rv64gc -mabi=lp64d -mcmodel=medany "$asm" "$SYLIB" \
             -o "$exe" >/dev/null 2>&1; then
            continue
        fi

        # 取多次运行的最小耗时（减少噪声），并校验输出正确
        local best=999999999 correct=1
        for ((r=0; r<REPS; r++)); do
            local out="$WORK/${name}.out"
            local t0 t1
            t0=$(now_ms)
            if [ -f "$infile" ]; then
                timeout ${TIMEOUT_SEC} $QEMU "$exe" < "$infile" > "$out" 2>/dev/null
            else
                timeout ${TIMEOUT_SEC} $QEMU "$exe" > "$out" 2>/dev/null
            fi
            local rc=$?
            t1=$(now_ms)
            # 追加返回码到输出（SysY 约定：main 返回值算作输出一部分）
            printf '%s\n' "$rc" >> "$out"
            local dt=$((t1 - t0))
            (( dt < best )) && best=$dt
            # 正确性：与 expected 严格比对（规范化尾随空白）
            if [ -f "$expected" ]; then
                if ! diff -q <(sed -e 's/[[:space:]]*$//' "$out") \
                             <(sed -e 's/[[:space:]]*$//' "$expected") >/dev/null 2>&1; then
                    correct=0
                fi
            fi
        done
        if [ "$correct" = 1 ]; then run_ok=$((run_ok+1)); else run_bad=$((run_bad+1)); fi
        total_ms=$((total_ms + best))
    done
    printf "%-4s compile=%-3d correct=%-3d WRONG=%-3d  total=%d ms\n" \
        "$label" "$compile_ok" "$run_ok" "$run_bad" "$total_ms"
    echo "$total_ms" > "$WORK/${label}.total"
}

echo "=================================================="
echo " A/B 实验：coalescing × GVN （每例取 ${REPS} 次最小耗时）"
echo "=================================================="
run_version "V0" "RA_COALESCE_MODE=rmw"
run_version "V1" "RA_COALESCE_MODE=full"
run_version "V2" "RA_COALESCE_MODE=rmw  OPT_ENABLE_GVN=1"
run_version "V3" "RA_COALESCE_MODE=full OPT_ENABLE_GVN=1"

echo "--------------------------------------------------"
v0=$(cat "$WORK/V0.total"); v1=$(cat "$WORK/V1.total")
v2=$(cat "$WORK/V2.total"); v3=$(cat "$WORK/V3.total")
echo " V0 (v3.1.0 基线)          : ${v0} ms"
echo " V1 (v3.2.0 coalescing)    : ${v1} ms   Δvs V0: $((v1-v0)) ms"
echo " V2 (旧coalescing + GVN)   : ${v2} ms   Δvs V0: $((v2-v0)) ms  ← 应复现回退"
echo " V3 (新coalescing + GVN)   : ${v3} ms   Δvs V1: $((v3-v1)) ms  ← 关键"
echo "--------------------------------------------------"
if (( v3 < v1 )); then
    echo " 结论：V3<V1 → coalescing 解锁 GVN，建议落地 P2（正式启用 GVN）"
elif (( v3 <= v1 + (v1/100) )); then   # 容忍 1% 噪声
    echo " 结论：V3≈V1 → 未回退但收益有限，需 P1 深化（spill 模型/活跃区间收缩）"
else
    echo " 结论：V3>V1 → coalescing 不足以解锁 GVN，需更强手段"
fi
echo " 注意：WRONG>0 的版本说明该组合有正确性问题，其耗时不可信。"
