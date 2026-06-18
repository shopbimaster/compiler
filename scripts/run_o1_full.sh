#!/bin/bash
# ================================================================
# Run all O1 tests with results to file
# -O1（大写）→ 测评服务器级别，全部优化 (OALL = O1+O2+O3)
# 小写 -o1/-o2/-o3 → 本地逐级调试
# ================================================================
set +e
cd /mnt/d/VSCodeProjects/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
TMPDIR=/tmp/o1full
rm -rf $TMPDIR
mkdir -p $TMPDIR

RESULT_FILE=/tmp/o1full_results.txt
> $RESULT_FILE

run_suite() {
  local suite_name="$1"
  local test_dir="$2"
  local timeout_s="$3"
  local prefix="$4"
  local total=0 pass=0 compile_fail=0 link_fail=0 diff_fail=0 segfault=0 timeout=0

  echo "" | tee -a $RESULT_FILE
  echo "============================================================" | tee -a $RESULT_FILE
  echo "  ${suite_name} (O1)" | tee -a $RESULT_FILE
  echo "============================================================" | tee -a $RESULT_FILE
  echo "" | tee -a $RESULT_FILE

  for src in "${test_dir}"/*.sy; do
    [ -f "$src" ] || continue
    local name
    name=$(basename "$src" .sy)
    local asm="${TMPDIR}/${prefix}_${name}.S"
    local bin="${TMPDIR}/${prefix}_${name}_bin"
    local infile="${test_dir}/${name}.in"
    local outfile="${test_dir}/${name}.out"
    total=$((total + 1))

    # Compile
    if ! timeout 30 ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
      echo "  COMPILE_FAIL: ${name}" | tee -a $RESULT_FILE
      compile_fail=$((compile_fail + 1))
      continue
    fi

    # Link
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null; then
      echo "  LINK_FAIL:    ${name}" | tee -a $RESULT_FILE
      link_fail=$((link_fail + 1))
      continue
    fi

    # Run
    local ret=0
    if [ -f "$infile" ]; then
      timeout ${timeout_s} $QEMU "$bin" < "$infile" > "${TMPDIR}/${prefix}_${name}_out.txt" 2>/dev/null || ret=$?
    else
      timeout ${timeout_s} $QEMU "$bin" > "${TMPDIR}/${prefix}_${name}_out.txt" 2>/dev/null || ret=$?
    fi

    if [ $ret -eq 124 ]; then
      echo "  TIMEOUT:      ${name}" | tee -a $RESULT_FILE
      timeout=$((timeout + 1))
    elif [ $ret -eq 139 ]; then
      echo "  SEGFAULT:     ${name}" | tee -a $RESULT_FILE
      segfault=$((segfault + 1))
    elif [ ! -f "$outfile" ]; then
      pass=$((pass + 1))
    else
      head -n -1 "$outfile" > "${TMPDIR}/${prefix}_${name}_expect.txt" 2>/dev/null
      if [ ! -s "${TMPDIR}/${prefix}_${name}_expect.txt" ]; then
        > "${TMPDIR}/${prefix}_${name}_expect.txt"
      fi
      printf '%s\n' "$(cat "${TMPDIR}/${prefix}_${name}_out.txt")" > "${TMPDIR}/${prefix}_${name}_act.txt"
      printf '%s\n' "$(cat "${TMPDIR}/${prefix}_${name}_expect.txt")" > "${TMPDIR}/${prefix}_${name}_expect.txt"
      if diff -q "${TMPDIR}/${prefix}_${name}_act.txt" "${TMPDIR}/${prefix}_${name}_expect.txt" > /dev/null 2>&1; then
        pass=$((pass + 1))
      else
        echo "  OUTPUT_DIFF:  ${name}" | tee -a $RESULT_FILE
        diff_fail=$((diff_fail + 1))
      fi
    fi
  done

  echo "" | tee -a $RESULT_FILE
  echo "  Compile:  $((total - compile_fail)) OK, $compile_fail FAIL" | tee -a $RESULT_FILE
  echo "  Link:     $((total - compile_fail - link_fail)) OK, $link_fail FAIL" | tee -a $RESULT_FILE
  echo "  Runtime:  $pass OK, $diff_fail DIFF, $segfault SEGFAULT, $timeout TIMEOUT" | tee -a $RESULT_FILE
}

echo "O1 Full Test Suite - $(date)" | tee $RESULT_FILE
run_suite "Functional Tests" "${PWD}/test/functional" 5 "func"
run_suite "H_Functional Tests" "${PWD}/test/h_functional" 15 "hfunc"
run_suite "Performance Tests" "${PWD}/test/performance" 15 "perf"
echo "" | tee -a $RESULT_FILE
echo "Done at $(date)" | tee -a $RESULT_FILE